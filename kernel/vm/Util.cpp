#include <private/Vm.hpp>
#include <lib/PlacementNew.hpp>

namespace Npk::Private
{
    static void FreeAmapNow(EbrItem* item)
    {
        auto* map = reinterpret_cast<Amap*>(item);

        PoolFreeWired(map, sizeof(Amap), AmapTag);
    }

    static void EmptyAndRetireBook(Book& book, EbrCallback freeNow)
    {
        NPK_ASSERT(book.ranges.Empty());

        auto result = AcquireSxMutexExclusive(&book.mutex, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        NPK_ASSERT(result == NpkStatus::Success);

        BookRemoveRange(MakeBookSpan(book, 0, BookPageLimit),
            ReleaseRemovedPage, nullptr);
        ReleaseSxMutexExclusive(&book.mutex);

        FinishBook(book);
        book.retire.callback = freeNow;
        result = RcuCall(&book.retire);
        NPK_ASSERT(result == NpkStatus::Success);
    }

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

    bool AmapTryRefBook(Book* book)
    {
        NPK_ASSERT(book->kind == BookKind::Amap);

        AmapRef held = reinterpret_cast<Amap*>(book);
        if (!held.Valid())
            return false;
        held.Disown();

        return true;
    }

    void AmapUnrefBook(Book* book)
    {
        auto held = AmapRef::Adopt(reinterpret_cast<Amap*>(book));

        held.Release();
    }

    NpkStatus CreateAmap(Amap** map)
    {
        if (map == nullptr)
            return NpkStatus::InvalidArg;

        void* ptr = PoolAllocWired(sizeof(Amap), AmapTag);
        if (ptr == nullptr)
            return NpkStatus::Shortage;

        auto* newMap = new(ptr) Amap {};
        ResetBook(newMap->book, BookKind::Amap, BookBacking::Swap);
        newMap->refcount = 1;
        *map = newMap;

        return NpkStatus::Success;
    }

    void ReleaseRemovedPage(PageInfo* page, void* arg)
    {
        (void)arg;

        NPK_ASSERT(page != nullptr);

        auto& dom = MySystemDomain();
        auto result = AcquireMutex(&dom.liveLists.lock, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        NPK_ASSERT(result == NpkStatus::Success);

        bool shouldFree = true;
        if (LiveListIsListed(page->liveList))
        {
            LiveListUnlink(dom, page);
            dom.liveLists.stats.Add(TrimStat::Untracked, 1);
        }
        else if (page->liveList == LiveListIsolated)
        {
            page->liveList = LiveListDoomed;
            shouldFree = false;
        }
        else
            NPK_ASSERT(page->liveList == 0);

        ReleaseMutex(&dom.liveLists.lock);

        if (shouldFree)
            FreePage(page);
    }

    void BeginMappingRemovals(VmRange& range)
    {
        if (range.source != nullptr)
            range.source->book.removalsInFlight.FetchAdd(1);
        if (range.amapRef.Valid())
            range.amapRef->book.removalsInFlight.FetchAdd(1);
    }

    void EndMappingRemovals(VmRange& range)
    {
        if (range.source != nullptr)
        {
            const size_t prev = range.source->book.removalsInFlight.FetchSub(1);
            NPK_ASSERT(prev != 0);
        }

        if (range.amapRef.Valid())
        {
            const size_t prev = range.amapRef->book.removalsInFlight.FetchSub(1);
            NPK_ASSERT(prev != 0);
        }
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
        if (IsZeroPage(paddr))
            return NpkStatus::Success;
        if (!PaddrHasPageInfo(paddr))
            return NpkStatus::Unsupported;
        if (!dirtied)
            return NpkStatus::Success;

        BookMarkWritten(nullptr, LookupPageInfo(paddr));

        return NpkStatus::Success;
    }

    void DestroyAmap(Amap* map)
    {
        NPK_ASSERT(map != nullptr);
        NPK_ASSERT(map->refcount == 0);

        EmptyAndRetireBook(map->book, FreeAmapNow);
    }
}

namespace Npk
{
    //TODO: reflow these into intent-based helpers over the pmap.
    NpkStatus SetKernelMap(uintptr_t vaddr, Paddr paddr, VmFlags flags)
    {
        MmuPermissions perms {};
        MmuCacheMode mode = MmuCacheMode::Default;

        if (flags.Has(VmFlag::Write))
            perms.Set(MmuPermission::Write);
        if (flags.Has(VmFlag::Fetch))
            perms.Set(MmuPermission::Fetch);
        if (flags.Has(VmFlag::Mmio))
            mode = MmuCacheMode::Mmio;

        MapBatchRange rangeStore[1];
        MapBatch batch {};
        ResetMapBatch(batch, HwKernelMap(), { rangeStore, 1 });

        auto result = HwMapAdd(batch, vaddr, paddr, perms, mode, true);

        HwMapUpdate(batch, false);
        FinishMapBatch(batch);

        return result;
    }

    NpkStatus ClearKernelMap(MapBatch& batch, Paddr* paddr, uintptr_t vaddr)
    {
        if (paddr != nullptr)
        {
            auto result = HwMapExtract(paddr, nullptr, nullptr,
                HwKernelMap(), vaddr);

            if (result != NpkStatus::Success)
                return result;
        }
        auto result = HwMapRemove(batch, vaddr, 1);

        return result;
    }
}
