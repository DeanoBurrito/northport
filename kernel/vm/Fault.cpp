#include <private/Vm.hpp>
#include <lib/Maths.hpp>

namespace Npk::Private
{
    constexpr size_t MaxPageFaultAttempts = 4;
    constexpr size_t MaxFaultRetryRounds = 32;
    constexpr size_t MaxUnlockedFaultRounds = 4;
    constexpr size_t MaxFaultMemoryWaits = 16;
    constexpr auto WaitForMemoryTimeout = 100_ms;
    constexpr size_t MaxFaultAddRanges = 1;

    struct ClusterWindow
    {
        size_t first;
        size_t count;
        size_t mainIndex;
    };

    struct SourceFaultResult
    {
        PageInfo* pages[MaxClusterPages];
        size_t count;
        size_t mainIndex;
        uintptr_t page0Addr;
    };

    static size_t GrowClusterWindow(size_t current)
    {
        //numbers stolen from linux (`get_next_ra_size()`).
        if (current < MaxClusterPages / 16)
            return 4 * current;
        if (current <= MaxClusterPages / 2)
            return 2 * current;

        return MaxClusterPages;
    }

    //NOTE: assumes `range.mutex` is held in either mode.
    static ClusterWindow ChooseClusterWindow(VmRange& range, size_t slot)
    {
        const auto& book = range.source->book;
        const bool single = book.backing == BookBacking::Swap;
        const uint32_t nextSlot = range.nextFaultSlotPlusOne.Load(sl::Relaxed);
        const bool linear = nextSlot == (slot + 1);
        const auto adv = range.advice;

        size_t target = 1;
        if (single || MemoryIsCritical() || adv == VmAdvice::Random)
            target = 1;
        else if (adv == VmAdvice::WillNeed || adv == VmAdvice::Sequential)
            target = MaxClusterPages;
        else
        {
            target = range.readaheadWindow.Load(sl::Relaxed);

            if (target == 0)
                target = GetClusterInPageCount();
            else if (linear)
                target = GrowClusterWindow(target);
            else
            {
                auto score = BookReadaheadScore(book);

                score = (score * MaxClusterPages) >> 8;
                target = sl::Max(score, target / 2);
            }

            target = sl::Clamp<size_t>(target, 1, MaxClusterPages);
            range.readaheadWindow.Store(static_cast<uint8_t>(target),
                sl::Relaxed);

            if (MemoryIsLow())
                target = sl::Max<size_t>(target / 4, 1);
        }

        ClusterWindow window {};
        if (linear || adv == VmAdvice::Sequential || target == 1)
            window.first = slot;
        else
        {
            const size_t back = (target - 1) / 2;

            window.first = slot - back;
            if (slot <= back)
                window.first = 0;
        }

        const size_t rangeLen = range.length.Load(sl::Relaxed) >> PfnShift();
        const size_t last = sl::Min(window.first + target, rangeLen);

        window.count = last - window.first;
        window.mainIndex = slot - window.first;
        range.nextFaultSlotPlusOne.Store(static_cast<uint32_t>(last + 1),
            sl::Relaxed);

        return window;
    }

    //NOTE: assumes `range.mutex` is held shared, and `range` covers `addr`.
    static NpkStatus DropPreviousMapping(Paddr* prev, bool* prevDirtied,
        VmRange& range, uintptr_t addr)
    {
        auto* map = range.space->map;
        *prevDirtied = false;

        auto result = HwMapExtract(prev, nullptr, nullptr, map, addr);

        if (result == NpkStatus::BadVaddr)
            return NpkStatus::NotAvailable;
        if (result != NpkStatus::Success)
            return result;

        //include slack space for any mmu structures to free, since we're
        //removing a mapping.
        MapBatchRange rangeStore[1 + MapBatchTableSlack];
        MapBatch batch {};
        ResetMapBatch(batch, map, rangeStore);

        BeginMappingRemovals(range);
        const auto harvest = HwMapHarvest(batch, addr, *prev,
            HwHarvestAction::Remove, {});

        if (!harvest.removed)
        {
            FinishMapBatch(batch);
            EndMappingRemovals(range);

            return NpkStatus::NotAvailable;
        }
        *prevDirtied = harvest.state.Has(MmuPageState::Dirty);

        HwMapUpdate(batch, true);
        FinishMapBatch(batch);

        EndMappingRemovals(range);

        return NpkStatus::Success;
    }

    //NOTE: assumes `range.mutex` is held exclusive.
    static NpkStatus EnsurePrivateAmap(VmRange& range)
    {
        if (!range.amapRef.Valid())
        {
            //no amap at all, alloc a new one.

            Amap* ptr;
            auto result = CreateAmap(&ptr);
            if (result != NpkStatus::Success)
                return result;

            range.amapOffset = 0;
            LinkLayer(ptr->book, range.upperLink, &range);
            range.amapRef = AmapRef::Adopt(ptr);
            range.flags.Clear(VmFlag::AmapNeedsCopy);
            range.version++;

            return NpkStatus::Success;
        }

        if (!range.flags.Has(VmFlag::AmapNeedsCopy))
            return NpkStatus::Success; //has amap, private already.

        //we have a shared amap, clone it!
        auto& from = range.amapRef->book;
        const size_t first = range.amapOffset;
        const size_t pages = range.length.Load(sl::Relaxed) >> PfnShift();
        BookSpan todo = MakeBookSpan(from, first, first + pages);

        Amap* fresh = nullptr;
        BookPromoteSpares spares {};
        auto status = NpkStatus::Success;
        while (true)
        {
            auto result = AcquireSxMutexExclusive(&from.mutex, sl::NoTimeout,
                NPK_WAIT_LOCATION);
            NPK_ASSERT(result == NpkStatus::Success);

            if (todo.begin == first && !BookOverlapped(todo, &range.upperLink))
            {
                range.flags.Clear(VmFlag::AmapNeedsCopy);
                ReleaseSxMutexExclusive(&from.mutex);
                break;
            }

            if (fresh == nullptr)
            {
                ReleaseSxMutexExclusive(&from.mutex);
                status = CreateAmap(&fresh);
                if (status != NpkStatus::Success)
                    break;
                continue;
            }

            BookPromoteNeed need;
            BookFill* fill;
            status = BookPromote(todo, first, fresh->book, spares, need, &fill);
            if (status == NpkStatus::Success)
            {
                auto& to = fresh->book;
                BookUnlinkRange(from, range.upperLink);

                result = AcquireSxMutexExclusive(&to.mutex, sl::NoTimeout,
                    NPK_WAIT_LOCATION);
                NPK_ASSERT(result == NpkStatus::Success);

                range.amapOffset = 0;
                BookLinkRange(to, range.upperLink);
                ReleaseSxMutexExclusive(&to.mutex);
                ReleaseSxMutexExclusive(&from.mutex);

                //the old amap's reference goes last, at passive
                AmapRef old = sl::Move(range.amapRef);
                range.amapRef = AmapRef::Adopt(fresh);
                fresh = nullptr;
                range.flags.Clear(VmFlag::AmapNeedsCopy);
                range.version++;

                old.Release();
                break;
            }
            ReleaseSxMutexExclusive(&from.mutex);

            if (status == NpkStatus::Pending)
            {
                BookWaitFill(fill);
                status = NpkStatus::Success;
                continue;
            }

            NPK_ASSERT(status == NpkStatus::Shortage);
            status = BookFillPromoteSpares(spares, need);
            if (status != NpkStatus::Success)
                break;
        }

        BookDrainPromoteSpares(spares);

        if (fresh != nullptr)
            AmapRef::Adopt(fresh).Release();

        return status;
    }

    static bool CopyPageWithMetadata(PageInfo* dst, PageInfo* src)
    {
        const auto prevIpl = EnsureIpl(Ipl::Dpc);
        auto dest = AccessPage(dst);
        auto from = AccessPage(src);

        const bool ok = dest.vaddr && from.Valid();
        if (ok)
            sl::MemCopy(dest.vaddr, from.vaddr, PageSize());

        DestroyPageAccess(&dest);
        DestroyPageAccess(&from);
        RestoreIpl(prevIpl);

        if (ok)
        {
            const auto dstAddr = LookupPagePaddr(dst);
            const auto srcAddr = LookupPagePaddr(src);

            HwCopyPageMetadata(dstAddr, srcAddr);
        }

        return ok;
    }

    //NOTE: assumes `range.mutex` is held and it's amap is private.
    static NpkStatus PopulateAmapSlot(PageInfo** out, VmRange& range,
        size_t slot, PageInfo* copyFrom)
    {
        auto* page = AllocPage(true);
        if (page == nullptr)
            return NpkStatus::Shortage;

        if (copyFrom != nullptr && !CopyPageWithMetadata(page, copyFrom))
        {
            FreePage(page);

            return NpkStatus::Shortage;
        }

        const auto result = BookPopulate(range.amapRef->book, slot, page);
        if (result != NpkStatus::Success)
        {
            FreePage(page);

            return result;
        }

        TrackPage(page);
        *out = page;

        return NpkStatus::Success;
    }

    static NpkStatus SwapInCell(AnonCell* cell)
    {
        BookFill* fill;
        auto result = BookBeginCellSwapIn(&fill, cell);

        if (result == NpkStatus::NotFound)
            return NpkStatus::Retry;
        if (result != NpkStatus::Success)
            return result;

        PageInfo* page = AllocPage(true);
        result = NpkStatus::Shortage;
        if (page != nullptr)
        {
            SwapOp op {};
            result = SwapBeginRead(op, { fill->handles, 1 }, { &page, 1 });

            if (result == NpkStatus::Success || result == NpkStatus::Pending)
                result = SwapEndRead(op);
        }

        const bool hasSuccess = result == NpkStatus::Success;
        if (BookEndCellSwapIn(cell, fill, page, hasSuccess))
            TrackPage(page);

        if (result == NpkStatus::NotFound)
        {
            Log("Shared anon page has handle with no location: contents lost!",
                LogLevel::Error);

            return NpkStatus::InternalError;
        }
        if (result != NpkStatus::Success)
            return result;

        return NpkStatus::Retry;
    }

    static void ReleasePinnedAnon(PageInfo** page)
    {
        if (*page == nullptr)
            return;

        BookUnpin(*page);
        *page = nullptr;
    }

    static void ReleaseSourceCluster(SourceFaultResult& fetched)
    {
        for (size_t i = 0; i < fetched.count; i++)
        {
            auto& page = fetched.pages[i];

            if (page == nullptr)
                continue;

            BookUnpin(page);
            page = nullptr;
        }
    }

    static void UnmapOwnEntry(VmRange& range, uintptr_t addr, PageInfo* page)
    {
        auto* map = range.space->map;
        const Paddr frame = LookupPagePaddr(page);
        Paddr present;
        if (HwMapExtract(&present, nullptr, nullptr, map, addr)
            != NpkStatus::Success || present != frame)
            return;

        MapBatchRange store[1 + MapBatchTableSlack];
        MapBatch batch {};
        ResetMapBatch(batch, map, store);
        BeginMappingRemovals(range);

        const auto action = HwHarvestAction::Remove;
        const auto harvest = HwMapHarvest(batch, addr, frame, action, {});
        if (harvest.removed)
        {
            HwMapUpdate(batch, true);
            (void)SettleMappedPage(frame,
                harvest.state.Has(MmuPageState::Dirty));
        }

        FinishMapBatch(batch);
        EndMappingRemovals(range);
    }

    //NOTE: no locks required, acquires internally and re-checks input pages
    //against book's size. Any races on a book are caught and resolved here.
    static void CommitFaultedPages(VmRange& range, SourceFaultResult& fetched,
        const bool* mapped)
    {
        auto& src = *range.source;

        const auto locked = AcquireSxMutexShared(&src.book.mutex,
            sl::NoTimeout, NPK_WAIT_LOCATION);
        NPK_ASSERT(locked == NpkStatus::Success);

        const auto extent = GetSourceSize(&src);
        uint64_t limit = static_cast<uint64_t>(-1);
        if (extent.bounded)
            limit = PageCountFor(extent.size);

        for (size_t i = 0; i < fetched.count; i++)
        {
            PageInfo* page = fetched.pages[i];
            if (page == nullptr)
                continue;

            if (mapped[i] && page->offset < limit)
            {
                BookCommitLocked(src.book, page);
                continue;
            }

            if (mapped[i])
            {
                UnmapOwnEntry(range, fetched.page0Addr + (i << PfnShift()),
                    page);
            }
            BookUnpin(page);
        }

        ReleaseSxMutexShared(&src.book.mutex);
    }

    //NOTE: assumes `range.mutex` is not held, and was held shared before the
    //caller dropped it. Retakes it shared, false if the range changed.
    static bool RelockAfterIo(VmRange& range, uint32_t version)
    {
        const auto result = AcquireSxMutexShared(&range.mutex, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        NPK_ASSERT(result == NpkStatus::Success);

        if (range.space->faultGate.Load(sl::Acquire))
            return false;
        if ((range.faultRefs.Load(sl::Acquire) & VmRangeDying) != 0)
            return false;

        return version == range.version;
    }

    //NOTE: `found` is a resident page managed by a cell.
    static NpkStatus FaultOnCell(PageInfo** out, const BookLookup& found,
        VmRange& range, uintptr_t addr, size_t slot, bool write)
    {
        auto& book = range.amapRef->book;
        AnonCell* cell = found.cell;
        NPK_ASSERT(found.result == BookFindResult::Page);

        PageInfo* page = found.page;
        if (!write || !found.shared)
        {
            //this book is the only sharer: try to end the cell before mapping
            if (!found.shared)
                BookTryUnpromote(book, slot, cell, page);

            BookUnrefCell(cell);
            *out = page;

            return NpkStatus::Success;
        }

        PageInfo* fresh = AllocPage(true);
        if (fresh == nullptr || !CopyPageWithMetadata(fresh, page))
        {
            if (fresh != nullptr)
                FreePage(fresh);

            BookUnpin(page);
            BookUnrefCell(cell);

            return NpkStatus::Shortage;
        }

        Paddr prev;
        bool prevDirtied;
        auto result = DropPreviousMapping(&prev, &prevDirtied, range, addr);
        if (result == NpkStatus::Success)
            SettleMappedPage(prev, prevDirtied);
        else if (result != NpkStatus::NotAvailable)
        {
            FreePage(fresh);
            BookUnpin(page);
            BookUnrefCell(cell);

            return result;
        }

        const auto outcome = BreakCowOnBookCell(book, slot, cell, page, fresh);
        BookUnrefCell(cell);

        switch (outcome)
        {
        case BookBreak::UsedFresh:
            TrackPage(fresh);
            *out = fresh;
            return NpkStatus::Success;

        case BookBreak::ReusedOld:
            FreePage(fresh);
            *out = page;
            return NpkStatus::Success;

        case BookBreak::TryAgain:
            FreePage(fresh);
            BookUnpin(page);
            return NpkStatus::Retry;
        }
        NPK_UNREACHABLE();
    }

    //NOTE: assumes `range.mutex` is held shared, and returns with it held
    //shared. Unless `holdLocked` is set it is dropped around any wait, and
    //`Retry` is returned if the range changed meanwhile.
    static NpkStatus FaultOnAnonPage(PageInfo** out, bool* viaCell,
        VmRange& range, uintptr_t addr, size_t slot, bool write,
        bool holdLocked)
    {
        auto& book = range.amapRef->book;

        while (true)
        {
            const auto found = BookLookupPin(book, slot, write);
            *viaCell = found.cell != nullptr;

            if (found.result == BookFindResult::Empty)
                return NpkStatus::NotAvailable;

            if (found.result == BookFindResult::Page)
            {
                if (found.cell == nullptr)
                {
                    *out = found.page;

                    return NpkStatus::Success;
                }

                const auto result = FaultOnCell(out, found, range, addr,
                    slot, write);
                if (result == NpkStatus::Retry)
                    continue;

                return result;
            }

            //keeps `book` alive while the range's mutex is dropped
            AmapRef held = range.amapRef;
            const uint32_t version = range.version;
            if (!holdLocked)
                ReleaseSxMutexShared(&range.mutex);

            PageInfo* page = nullptr;
            auto result = NpkStatus::Retry;
            if (found.result == BookFindResult::Filling)
                BookWaitFill(found.fill);
            else if (found.cell != nullptr)
                result = SwapInCell(found.cell);
            else
            {
                const size_t window = GetReadaheadWindowFor(book, slot);
                result = SwapInBookEntry(&page, book, slot, window);
            }

            if (found.cell != nullptr)
                BookUnrefCell(found.cell);

            bool current = true;
            if (!holdLocked)
                current = RelockAfterIo(range, version);

            if (result == NpkStatus::Success && !current)
            {
                BookUnpin(page);
                result = NpkStatus::Retry;
            }
            if (result == NpkStatus::Retry && current)
                continue;
            if (result != NpkStatus::Success)
                return result;

            if (write)
                BookMarkWritten(&book, page);
            *out = page;

            return NpkStatus::Success;
        }
    }

    //NOTE: assumes `range.mutex` is held shared, `holdLocked` determines if
    //the mutex is dropped around calling into the pager.
    static NpkStatus FaultOnSource(SourceFaultResult& out, VmRange& range,
        size_t page, bool write, bool holdLocked)
    {
        auto& src = *range.source;
        const auto window = ChooseClusterWindow(range, page);
        const size_t baseIndex = (range.offset >> PfnShift()) + window.first;
        const size_t first = window.first;

        out.count = window.count;
        out.mainIndex = window.mainIndex;
        out.page0Addr = range.base + (first << PfnShift());

        PagerFlags flags {};
        if (write && !range.flags.Has(VmFlag::CopyOnWrite))
            flags.Set(PagerFlag::Write);

        const uint32_t version = range.version;
        if (!holdLocked)
            ReleaseSxMutexShared(&range.mutex);

        auto result = SourcePinCluster(src, { out.pages, out.count }, baseIndex,
            out.mainIndex, flags);

        if (holdLocked)
            return result;

        const bool current = RelockAfterIo(range, version);
        const auto extent = GetSourceSize(&src);
        if (result == NpkStatus::Success && extent.bounded
            && baseIndex + out.mainIndex >= PageCountFor(extent.size))
        {
            ReleaseSourceCluster(out);
            out.count = 0;

            return NpkStatus::BadVaddr;
        }

        if (current)
            return result;

        if (result == NpkStatus::Success)
            ReleaseSourceCluster(out);
        out.count = 0;

        return NpkStatus::Retry;
    }
    static_assert(MaxClusterPages <= MaxSourceClusterPages);

    //NOTE: assumes `range.mutex` is held shared, that `range` includes
    //`addr`, and that a write to a copy-on-write range already has a private
    //amap.
    static NpkStatus AttemptPageFaultLocked(VmRange& range, uintptr_t addr,
        bool write, bool holdLocked)
    {
        //0. Object-level permissions
        if (write && !range.flags.Has(VmFlag::Write))
            return NpkStatus::NotWritable;

        const size_t page = (addr - range.base) >> PfnShift();
        const bool isCow = range.flags.Has(VmFlag::CopyOnWrite);
        auto* map = range.space->map;

        auto result = NpkStatus::Success;

        Paddr paddr {};
        bool resolved = false;
        bool viaCell = false;
        PageInfo* pinnedAnon = nullptr;

        //1. If we have an amap try resolve the fault through it.
        const size_t slot = range.amapOffset + page;
        if (range.amapRef.Valid())
        {
            PageInfo* anon;
            result = FaultOnAnonPage(&anon, &viaCell, range, addr, slot, write,
                holdLocked);

            if (result == NpkStatus::Success)
            {
                pinnedAnon = anon;
                paddr = LookupPagePaddr(anon);
                resolved = true;
            }
            else if (result != NpkStatus::NotAvailable)
                return result;
        }

        //2. Amap couldn't resolve it, try resolve the fault at the source layer
        SourceFaultResult srcResult {};
        bool haveSrcResult = false;
        if (!resolved && range.source != nullptr)
        {
            result = FaultOnSource(srcResult, range, page, write, holdLocked);
            if (result != NpkStatus::Success)
                return result;
            haveSrcResult = true;

            auto* mainPage =  srcResult.pages[srcResult.mainIndex];
            if (write && isCow)
            {
                result = PopulateAmapSlot(&pinnedAnon, range, slot,
                    mainPage);
                if (result == NpkStatus::Exists)
                    result = NpkStatus::Retry;
                if (result != NpkStatus::Success)
                {
                    ReleaseSourceCluster(srcResult);

                    return result;
                }

                paddr = LookupPagePaddr(pinnedAnon);
                srcResult.pages[srcResult.mainIndex] = nullptr;
                BookUnpin(mainPage);
            }
            else
                paddr = LookupPagePaddr(mainPage);

            resolved = true;
        }

        //3. Source layer had nothing, so it's a zero-fill region.
        if (!resolved && isCow)
        {
            if (write)
            {
                result = PopulateAmapSlot(&pinnedAnon, range, slot, nullptr);
                if (result == NpkStatus::Exists)
                    return NpkStatus::Retry;
                if (result != NpkStatus::Success)
                    return result;

                paddr = LookupPagePaddr(pinnedAnon);
            }
            else
                paddr = sysDomain0.zeroPage;
            resolved = true;
        }

        //4. If we dont have a page at this point, its a bad fault.
        //That means the amap and source layers rejected the slot, and the range
        //isn't marked as CoW.
        if (!resolved)
            return NpkStatus::BadVaddr;

        auto AmapStillAgrees = [](VmRange& range, size_t slot, PageInfo* anon,
            bool viaCell) -> bool
        {
            if (!range.amapRef.Valid())
                return true;

            auto& book = range.amapRef->book;
            if (anon == nullptr)
                return !BookHasEntry(book, slot);

            //only a cell's entry can be broken to another page under a pin
            if (!viaCell)
                return true;

            const auto found = BookLookupPin(book, slot, false);
            const bool agrees = found.result == BookFindResult::Page
                && found.page == anon;

            if (found.result == BookFindResult::Page)
                BookUnpin(found.page);
            else if (found.result == BookFindResult::Filling)
                BookLeaveFill(found.fill);
            if (found.cell != nullptr)
                BookUnrefCell(found.cell);

            return agrees;
        };

        //5. The decision may be stale by now: recheck it and install as one
        //step against the other faults in this range.
        result = AcquireMutex(&range.installMutex, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        NPK_ASSERT(result == NpkStatus::Success);

        if (!AmapStillAgrees(range, slot, pinnedAnon, viaCell))
        {
            ReleaseMutex(&range.installMutex);
            ReleaseSourceCluster(srcResult);
            ReleasePinnedAnon(&pinnedAnon);

            return NpkStatus::Retry;
        }

        //another fault already installed what this one would
        Paddr present;
        MmuPermissions presentPerms {};
        result = HwMapExtract(&present, &presentPerms, nullptr, map, addr);
        const bool alreadyMapped = result == NpkStatus::Success
            && present == paddr
            && (!write || presentPerms.Has(MmuPermission::Write));
        if (alreadyMapped)
        {
            ReleaseMutex(&range.installMutex);
            ReleaseSourceCluster(srcResult);
            ReleasePinnedAnon(&pinnedAnon);

            return NpkStatus::Success;
        }

        //helper lambda
        auto ConvertFlags = [](MmuPermissions& perms, MmuCacheMode& mode,
            VmFlags flags, bool write) -> void
        {
            if (write)
                perms.Set(MmuPermission::Write);
            if (flags.Has(VmFlag::Fetch))
                perms.Set(MmuPermission::Fetch);

            mode = MmuCacheMode::Default;
            if (flags.Has(VmFlag::Mmio))
                mode = MmuCacheMode::Mmio;
        };

        MmuPermissions perms {};
        MmuCacheMode mode {};
        ConvertFlags(perms, mode, range.flags, write);

        //6. Remove an old mapping and install the new mapping. We allow for a
        //fastpath with `HwMapUpgrade()` here.
        bool upgraded = false;
        if (write)
        {
            //optimization: try upgrade the mapping if the MMU allows this.
            MapBatchRange upgradeStore[1];
            MapBatch upgradeBatch {};
            ResetMapBatch(upgradeBatch, map, upgradeStore);

            upgraded = HwMapUpgrade(upgradeBatch, addr, paddr, perms).matched;

            HwMapUpdate(upgradeBatch, true);
            FinishMapBatch(upgradeBatch);
        }

        //no fastpath, do it the long way.
        Paddr prevPaddr;
        bool prevDirtied = false;
        result = NpkStatus::NotAvailable;
        if (!upgraded)
            result = DropPreviousMapping(&prevPaddr, &prevDirtied, range, addr);
        if (result == NpkStatus::Success)
            result = SettleMappedPage(prevPaddr, prevDirtied);

        if (result != NpkStatus::Success && result != NpkStatus::NotAvailable)
        {
            ReleaseMutex(&range.installMutex);
            ReleaseSourceCluster(srcResult);
            ReleasePinnedAnon(&pinnedAnon);

            return result;
        }

        MapBatchRange rangeStore[MaxFaultAddRanges];
        MapBatch batch {};
        ResetMapBatch(batch, map, { rangeStore, MaxFaultAddRanges });

        //add the new mapping
        result = NpkStatus::Success;
        if (!upgraded)
            result = HwMapAdd(batch, addr, paddr, perms, mode, false);
        if (result != NpkStatus::Success)
        {
            ReleaseSourceCluster(srcResult);
            ReleasePinnedAnon(&pinnedAnon);
            FinishMapBatch(batch);
            ReleaseMutex(&range.installMutex);

            return result;
        }

        //7. Housekeeping after adding the new mapping: the owner's pin becomes
        //the mapped flag.
        if (pinnedAnon != nullptr)
        {
            BookCommit(range.amapRef->book, pinnedAnon);
            pinnedAnon = nullptr;
        }

        //bonus round: map clustered pages from the source layer
        if (haveSrcResult)
        {
            //Clustered pages are always mapped readonly and never overwrite
            //an existing mapping.
            //They're readonly so we can detect readahead usage and feed that
            //into future readahead decisions, see `BookReadaheadFeedback` and
            //the `Readahead` page info flag.

            bool mapped[MaxClusterPages] {};
            MmuPermissions clusterPerms {};
            ConvertFlags(clusterPerms, mode, range.flags, false);

            for (size_t i = 0; i < srcResult.count; i++)
            {
                auto* page = srcResult.pages[i];
                if (page == nullptr)
                    continue;

                if (i == srcResult.mainIndex)
                {
                    mapped[i] = true;
                    continue;
                }

                const uintptr_t at = srcResult.page0Addr + (i << PfnShift());

                if (range.amapRef.Valid())
                {
                    const size_t amapSlot = range.amapOffset
                        + ((at - range.base) >> PfnShift());

                    if (BookHasEntry(range.amapRef->book, amapSlot))
                        continue;
                }

                Paddr existing;
                result = HwMapExtract(&existing, nullptr,
                    nullptr, map, at);

                const bool occupied = result == NpkStatus::Success;
                if (!occupied)
                {
                    result = HwMapAdd(batch, at, LookupPagePaddr(page),
                        clusterPerms, mode, false);
                }

                mapped[i] = !occupied && result == NpkStatus::Success;
            }

            CommitFaultedPages(range, srcResult, mapped);
        }

        FinishMapBatch(batch);
        ReleaseMutex(&range.installMutex);

        return NpkStatus::Success;
    }

    static NpkStatus AttemptPageFault(VmSpace& space, uintptr_t addr,
        bool write)
    {
        auto NeedsPrivateAmap = [](const VmRange& range) -> bool
        {
            if (!range.flags.Has(VmFlag::Write))
                return false;
            if (!range.flags.Has(VmFlag::CopyOnWrite))
                return false;
            if (!range.amapRef.Valid())
                return true;

            return range.flags.Has(VmFlag::AmapNeedsCopy);
        };

        auto PrivatiseAmap = [](VmRange& range) -> NpkStatus
        {
            auto result = AcquireSxMutexExclusive(&range.mutex,
                sl::NoTimeout, NPK_WAIT_LOCATION);
            if (result != NpkStatus::Success)
                return result;

            const bool gated = range.space->faultGate.Load(sl::Acquire);
            const bool dying = (range.faultRefs.Load(sl::Acquire)
                & VmRangeDying) != 0;
            if (!gated && !dying)
                result = EnsurePrivateAmap(range);

            ReleaseSxMutexExclusive(&range.mutex);

            return result;
        };

        for (size_t round = 0; ; round++)
        {
            VmRange* range;
            auto result = BeginFaultInRange(&range, space, addr);
            if (result != NpkStatus::Success)
                return result;

            result = AcquireSxMutexShared(&range->mutex, sl::NoTimeout,
                NPK_WAIT_LOCATION);
            if (result != NpkStatus::Success)
            {
                EndFaultInRange(*range);
                return result;
            }

            const bool gated = space.faultGate.Load(sl::Acquire);
            const bool dying = (range->faultRefs.Load(sl::Acquire)
                & VmRangeDying) != 0;
            const bool covers = addr - range->base
                < range->length.Load(sl::Relaxed);
            if (gated || dying || !covers)
            {
                ReleaseSxMutexShared(&range->mutex);
                EndFaultInRange(*range);

                //a dying range's span is going: the fault is on nothing
                if (dying)
                    return NpkStatus::BadVaddr;
                WaitForFaultGateOfSpace(space);
                continue;
            }

            if (write && NeedsPrivateAmap(*range))
            {
                ReleaseSxMutexShared(&range->mutex);
                result = PrivatiseAmap(*range);
                EndFaultInRange(*range);

                if (result != NpkStatus::Success)
                    return result;
                continue;
            }

            const bool holdLocked = round >= MaxUnlockedFaultRounds;
            result = AttemptPageFaultLocked(*range, addr, write, holdLocked);
            ReleaseSxMutexShared(&range->mutex);
            EndFaultInRange(*range);

            if (result != NpkStatus::Retry || round >= MaxFaultRetryRounds)
                return result;
        }
    }

    size_t GetReadaheadWindowFor(Book& book, size_t index)
    {
        if (MemoryIsLow())
            return 1;

        return BookSwapWindow(book, index, GetReadaheadPageCount());
    }
}

namespace Npk
{
    void DispatchPageFault(bool* probe, uintptr_t addr, bool write, bool user)
    {
        using namespace Private;

        LowerIpl(Ipl::Passive);
        const bool prevIntrs = IntrsOn();

        const auto userAddrs = HwGetUserAddressRange();
        const auto kernelAddrs = HwGetKernelAddressRanges();

        bool isUserAddr = false;
        bool isKernelAddr = false;
        if (addr >= userAddrs.base && addr < userAddrs.top)
            isUserAddr = true;
        else
        {
            for (size_t i = 0; i < kernelAddrs.Size(); i++)
            {
                if (addr >= kernelAddrs[i].base
                    && addr < kernelAddrs[i].top)
                {
                    isKernelAddr = true;
                    break;
                }
            }
        }


        NPK_ASSERT(isUserAddr || isKernelAddr);
        addr = AlignDownPage(addr);

        //sanity checks and get the associated address space for the fault.
        NpkStatus result = NpkStatus::Success;
        VmSpace* space = nullptr;
        if (user)
        {
            /*
            const auto userRange = HwGetUserAddressRange();
            if (!isUserAddr)
                Boom();
            //a range changing under every pager call is held across one
            if (addr < userRange.base || addr >= userRange.top)
                Boom();
            */
            NPK_UNREACHABLE(); //TODO: user address space handling
        }
        else
        {
            if (isUserAddr)
            {
                Panic("Kernel attempted to %s user memory at %p",
                    nullptr, write ? "write" : "read", addr);
            }

            space = MySystemDomain().kernelSpace;
        }
        NPK_ASSERT(space != nullptr);

        //allow hardware layer to intercept the fault, it may be mmu
        //housekeeping (manually setting A/D bits).
        if (HwHandleMinorFaultOnMap(space->map, addr, write))
        {
            IntrsExchange(prevIntrs);
            RaiseIpl(Ipl::Interrupt);

            return;
        }

        size_t attempts = 0;
        size_t memoryWaits = 0;
        while (attempts < MaxPageFaultAttempts)
        {
            result = AttemptPageFault(*space, addr, write);
            if (result == NpkStatus::Success)
                break;

            if (result == NpkStatus::Shortage)
            {
                if (memoryWaits >= MaxFaultMemoryWaits)
                    break;
                memoryWaits++;

                AddPageDebt(1);
                WaitForPages(WaitForMemoryTimeout);
                continue;
            }

            //some status indicate a permanent failure: we'll get the
            //same answer if we try the same operation again, something about
            //insanity, so abort now.
            bool shouldAbort = false;
            switch (result)
            {
            case NpkStatus::BadVaddr:
                [[fallthrough]];
            case NpkStatus::NotWritable:
                [[fallthrough]];
            case NpkStatus::InvalidArg:
                [[fallthrough]];
            case NpkStatus::Unsupported:
                shouldAbort = true;
                break;

            default:
                break;
            }

            if (shouldAbort)
                break;
            attempts++;
        }

        if (result != NpkStatus::Success)
        {
            if (probe == nullptr)
            {
                //TODO: user faults should send a signal instead
                NPK_UNEXPECTED_STATUS(result, LogLevel::Error);
                NPK_UNREACHABLE();
            }
            else
                *probe = true;
        }
        else if (probe != nullptr)
            *probe = false;

        IntrsExchange(prevIntrs);
        RaiseIpl(Ipl::Interrupt);
    }
}
