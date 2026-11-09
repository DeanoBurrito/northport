#include <private/Vm.hpp>

namespace Npk::Private
{
    void RegisterExactPoolZone(HeapTag tag, size_t size, ZoneSharing sharing)
    {
        PoolZoneDesc desc {};

        desc.tag = tag;
        desc.objectSize = size;
        desc.wired = true;
        desc.flags = PoolZoneFlag::ExactSize;
        if (sharing == ZoneSharing::CpuShared)
            desc.flags.Set(PoolZoneFlag::CpuShared);

        const auto result = RegisterPoolZone(desc);
        NPK_ASSERT(result == NpkStatus::Success);
    }

    void* AllocTrieNode(size_t size)
    {
        void* node = PoolAllocWired(size, TrieNodeTag);
        if (node == nullptr)
            return nullptr;

        const auto addr = reinterpret_cast<uintptr_t>(node);
        NPK_ASSERT((addr & TrieHeightMask) == 0);

        return node;
    }

    void FreeTrieNode(void* node, size_t size)
    {
        PoolFreeWired(node, size, TrieNodeTag);
    }

    void RetireTrieNode(void* node, EbrCallback freeNow)
    {
        auto* item = static_cast<EbrItem*>(node);
        item->callback = freeNow;

        const auto result = RcuCall(item);
        NPK_ASSERT(result == NpkStatus::Success);
    }

    LiveListHead& GetLiveListHead(SystemDomain& dom, LiveList which)
    {
        NPK_ASSERT(which != LiveList::Count);

        const auto index = static_cast<size_t>(which);

        return dom.liveLists.heads[index];
    }

    void LiveListLink(SystemDomain& dom, LiveList which, PageInfo* page)
    {
        NPK_ASSERT(page->liveList == 0);

        auto& head = GetLiveListHead(dom, which);
        const auto success = head.pages.PushBack(page);
        NPK_ASSERT(success);
        page->liveList = LiveListToId(which);
        head.length++;

        uint8_t age = PageAgeNone;
        if (which >= LiveList::Active0 && which <= LiveList::Active3)
        {
            const size_t gen = (size_t)which - (size_t)LiveList::Active0;
            age = static_cast<uint8_t>(gen);
        }
        page->age.Store(age, sl::Relaxed);
    }

    void LiveListUnlink(SystemDomain& dom, PageInfo* page)
    {
        NPK_ASSERT(page->liveList != 0);

        auto& head = GetLiveListHead(dom, IdToLiveList(page->liveList));
        NPK_ASSERT(head.length > 0);

        const bool success = head.pages.Remove(page);
        NPK_ASSERT(success);
        page->liveList = 0;
        head.length--;
    }

    void PinPage(PageInfo* page)
    {
        constexpr auto PinsLimit = (1 << (sizeof(page->pins) * 8)) - 1;
        NPK_ASSERT(page->pins < PinsLimit);

        page->pins++;
    }

    void UnpinPage(PageInfo* page, size_t count)
    {
        NPK_ASSERT(page->pins >= count);

        page->pins -= count;
    }

    void TrackPage(PageInfo* page)
    {
        auto& dom = MySystemDomain();

        PageVmOwnerType type;
        PageInfoGetVmOwner(type, page);

        LiveList to = LiveList::Active1;
        if (type == PageVmOwnerType::Source)
            to = LiveList::Probation;

        auto result = AcquireMutex(&dom.liveLists.lock, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        NPK_ASSERT(result == NpkStatus::Success);
        LiveListLink(dom, to, page);

        dom.liveLists.stats.Add(TrimStat::Tracked, 1);
        ReleaseMutex(&dom.liveLists.lock);
    }

    void UntrackPage(PageInfo* page)
    {
        NPK_ASSERT(page != nullptr);

        auto& dom = MySystemDomain();
        auto result = AcquireMutex(&dom.liveLists.lock, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        NPK_ASSERT(result == NpkStatus::Success);

        if (page->liveList != 0)
        {
            LiveListUnlink(dom, page);

            dom.liveLists.stats.Add(TrimStat::Untracked, 1);
        }

        ReleaseMutex(&dom.liveLists.lock);
    }

    void DrainSourcePages(VmSource& src)
    {
        NPK_ASSERT(src.ranges.Empty());

        //this lock also synchronizes with the trimmer looking at this source
        //from the perspective of a PageInfo's owner.
        auto result = AcquireSxMutexExclusive(&src.mutex, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        NPK_ASSERT(result == NpkStatus::Success);

        while (!src.pages.Empty())
        {
            auto* page = src.pages.PopFront();

            UntrackPage(page);
            ClearPageMapped(page);

            NPK_ASSERT(page->pins == 0);
            FreePage(page);
        }

        ReleaseSxMutexExclusive(&src.mutex);
    }

    void BeginMappingRemovals(VmRange& range)
    {
        if (range.source != nullptr)
            range.source->removalsInFlight.FetchAdd(1);
        if (range.amapRef.Valid())
            range.amapRef->removalsInFlight.FetchAdd(1);
    }

    void EndMappingRemovals(VmRange& range)
    {
        if (range.source != nullptr)
        {
            const size_t prev = range.source->removalsInFlight.FetchSub(1);
            NPK_ASSERT(prev != 0);
        }

        if (range.amapRef.Valid())
        {
            const size_t prev = range.amapRef->removalsInFlight.FetchSub(1);
            NPK_ASSERT(prev != 0);
        }
    }

    void SettleAnonPage(AnonMap& map, PageInfo* page, bool dirtied,
        bool unpin)
    {
        if (page == nullptr)
            return;

        auto ref = AnonMapLookup(map, page->offset);
        if (!ref.Valid())
        {
            //not good! This is a UAF as the page is not being tracked by the
            //amap now but we thought it was.
            Log("Attempted to release anon page %p from slot %u: empty slot",
                LogLevel::Error, page, page->offset);
            
            return;
        }

        SwapSlot stale = NoSwapSlot;

        ref->lock.Lock();
        NPK_ASSERT(ref->page == page);

        if (dirtied)
            stale = AnonMarkDirty(*ref, page);
        if (unpin)
            UnpinPage(page);

        ref->lock.Unlock();

        if (stale != NoSwapSlot)
            SwapFreeSlot(stale);
    }

    void SettleSourcePage(VmSource& src, PageInfo* page, bool dirtied,
        bool unpin)
    {
        if (page == nullptr)
            return;

        if (AcquireSxMutexExclusive(&src.mutex, sl::NoTimeout,
            NPK_WAIT_LOCATION) != NpkStatus::Success)
            return;

        if (dirtied)
            page->flags.Clear(PageVmFlag::Clean);
        if (unpin)
            UnpinPage(page);

        ReleaseSxMutexExclusive(&src.mutex);
    }

    NpkStatus SettleMappedPage(Paddr paddr, bool dirtied)
    {
        if (paddr == MySystemDomain().zeroPage)
            return NpkStatus::Success;
        if (!PaddrHasPageInfo(paddr))
            return NpkStatus::Unsupported;
        if (!dirtied)
            return NpkStatus::Success;

        auto* page = LookupPageInfo(paddr);
        PageVmOwnerType type;
        void* owner = PageInfoGetVmOwner(type, page);
        if (owner == nullptr)
            return NpkStatus::Success;

        switch (type)
        {
        case PageVmOwnerType::Source:
            SettleSourcePage(*static_cast<VmSource*>(owner), page, true,
                false);
            return NpkStatus::Success;

        case PageVmOwnerType::Anon:
            SettleAnonPage(*static_cast<AnonMap*>(owner), page, true, false);
            return NpkStatus::Success;

        default:
            NPK_UNREACHABLE();
        }
    }
}
