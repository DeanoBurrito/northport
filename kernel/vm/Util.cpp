#include <private/Vm.hpp>
#include <lib/PlacementNew.hpp>

namespace Npk::Private
{
    static void FreeAmapNow(EbrItem* item)
    {
        auto* map = reinterpret_cast<Amap*>(item);

        PoolFreeWired(map, sizeof(Amap), AmapTag);
    }

    static void UnpinAll(sl::Span<PageInfo*> pages)
    {
        for (size_t i = 0; i < pages.Size(); i++)
        {
            if (pages[i] == nullptr)
                continue;

            BookUnpin(pages[i]);
            pages[i] = nullptr;
        }
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

    static NpkStatus PinCluster(VmSource& src, sl::Span<PageInfo*> out,
        size_t base, size_t mainIndex, PagerFlags flags)
    {
        NPK_ASSERT(out.Size() != 0 && out.Size() <= MaxSourceClusterPages);
        NPK_ASSERT(mainIndex < out.Size());

        const size_t count = out.Size();
        auto& book = src.book;

        while (true)
        {
            bool wanted[MaxSourceClusterPages] {};
            BookFill* theirs = nullptr;

            //first pass, extract what the book has for each wanted page.
            for (size_t i = 0; i < count; i++)
            {
                out[i] = nullptr;

                if (i != mainIndex && BookHasEntry(book, base + i))
                    continue;

                const auto found = BookLookupPin(book, base + i, false);
                const auto kind = found.result;

                if (kind == BookFindResult::Page)
                    out[i] = found.page;
                else if (kind == BookFindResult::Empty)
                    wanted[i] = true;
                else if (kind == BookFindResult::Filling && i == mainIndex)
                    theirs = found.fill;
                else if (kind == BookFindResult::Filling)
                    BookLeaveFill(found.fill); //read-ahead: never wait on it
            }

            if (theirs != nullptr)
            {
                UnpinAll(out);
                BookWaitFill(theirs);
                continue;
            }

            //determine the run (start, count) of pages to read in.
            const bool needMain = wanted[mainIndex];
            size_t start = mainIndex;
            if (needMain)
            {
                while (start > 0 && wanted[start - 1])
                    start--;
            }
            else
            {
                start = mainIndex + 1;
                while (start < count && !wanted[start])
                    start++;
                if (start == count)
                    return NpkStatus::Success;
            }

            BookFill* fill;
            size_t fillMain = 0;
            if (needMain)
                fillMain = mainIndex - start;

            size_t claimable = count - start;
            claimable = sl::Min<size_t>(claimable, BookPageLimit
                - (base + start));

            if (claimable == 0)
                return NpkStatus::Success;
            const auto claim = MakeBookSpan(book, base + start,
                base + start + claimable);

            //claim the run within the book, signal our intent to others.
            auto result = BookBeginFill(fill, claim, fillMain);
            if (result != NpkStatus::Success && !needMain)
                return NpkStatus::Success;
            if (result == NpkStatus::Exists)
            {
                UnpinAll(out);
                continue;
            }
            if (result != NpkStatus::Success)
            {
                UnpinAll(out);

                return result;
            }

            //at this point the claim promises to hold the main entry,
            //but the cluster we asked for may have been truncated either side
            //depending on what was available.
            start = fill->first - base;
            const size_t claimed = fill->count;

            PageInfo* got[MaxSourceClusterPages] {};
            PageInfo* span[MaxSourceClusterPages] {};

            bool shortage = false;
            for (size_t i = 0; i < claimed; i++)
            {
                got[i] = AllocPage(true);

                if (got[i] != nullptr)
                    PageClearFlag(got[i], PageVmFlag::Filled);
                else if (start + i == mainIndex)
                    shortage = true;

                span[start + i] = got[i];
            }

            NpkStatus pagerResult = NpkStatus::Shortage;
            if (!shortage)
            {
                pagerResult = src.ops->Get(&src, { span, count }, base,
                    mainIndex, nullptr, flags);
            }

            bool mainFilled = false;
            if (!shortage && needMain)
            {
                auto* main = got[mainIndex - start];
                mainFilled = PageHasFlag(main, PageVmFlag::Filled);
            }

            //create the bitmap of which pages were filled in as readahead,
            //so that they can be marked as such.
            static_assert(BookFillLimit < 32);

            uint32_t ahead = (1u << claimed) - 1;
            if (needMain)
                ahead &= ~(1u << (mainIndex - start));
            const uint32_t published = BookEndFill(book, fill,
                { got, claimed }, ahead);

            for (size_t i = 0; i < claimed; i++)
            {
                if ((published & (1u << i)) == 0)
                    continue;

                TrackPage(got[i]);
                out[start + i] = got[i];
            }

            if (out[mainIndex] != nullptr)
                return NpkStatus::Success;

            UnpinAll(out);

            if (mainFilled)
                continue;
            if (pagerResult == NpkStatus::Success)
                return NpkStatus::Shortage;

            return pagerResult;
        }
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

    void TrackPage(PageInfo* page)
    {
        auto& dom = MySystemDomain();

        PageVmOwnerType type;
        void* owner = PageInfoGetVmOwner(type, page);
        NPK_ASSERT(owner != nullptr);

        LiveList to = LiveList::Active1;
        if (type == PageVmOwnerType::Book
            && static_cast<Book*>(owner)->kind == BookKind::Source)
            to = LiveList::Probation;

        auto result = AcquireMutex(&dom.liveLists.lock, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        NPK_ASSERT(result == NpkStatus::Success);

        LiveListLink(dom, to, page);
        dom.liveLists.stats.Add(TrimStat::Tracked, 1);
        ReleaseMutex(&dom.liveLists.lock);
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

    void DestroySource(VmSource& src, EbrCallback freeNow)
    {
        EmptyAndRetireBook(src.book, freeNow);
    }

    NpkStatus SourcePinCluster(VmSource& src, sl::Span<PageInfo*> out,
        size_t base, size_t mainIndex, PagerFlags flags)
    {
        const auto result = PinCluster(src, out, base, mainIndex, flags);

        if (result != NpkStatus::Success)
            return result;

        const auto swapBacked = src.book.backing == BookBacking::Swap;
        if (flags.Has(PagerFlag::Write) && swapBacked)
            BookMarkWritten(&src.book, out[mainIndex]);

        return result;
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

    NpkStatus SourcePagePin(PageInfo** out, VmSource* source, size_t index)
    {
        if (out == nullptr || source == nullptr)
            return NpkStatus::InvalidArg;
        if (source->ops->Get == nullptr)
            return NpkStatus::Unsupported;

        PageInfo* pages[1] = {};
        const auto result = Private::SourcePinCluster(*source, { pages, 1 },
            index, 0, {});

        if (result == NpkStatus::Success)
            *out = pages[0];

        return result;
    }

    void SourcePageUnpin(VmSource* source, PageInfo* page)
    {
        (void)source;

        Private::BookUnpin(page);
    }

    bool SourcePageResident(VmSource* source, size_t index)
    {
        using namespace Private;

        const auto found = BookLookupPin(source->book, index, false);

        if (found.result == Private::BookFindResult::Page)
        {
            Private::BookUnpin(found.page);
            return true;
        }

        if (found.result == Private::BookFindResult::Filling)
            BookWaitFill(found.fill);

        return false;
    }
}
