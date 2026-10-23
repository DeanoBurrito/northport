#include <private/Vm.hpp>
#include <lib/Maths.hpp>

namespace Npk::Private
{
    constexpr size_t MaxPageFaultAttempts = 4;
    constexpr size_t MaxFaultMemoryWaits = 16;
    constexpr auto WaitForMemoryTimeout = 100_ms;
    constexpr size_t MaxClusterPages = 16;
    constexpr size_t DefaultClusterPages = 8;
    constexpr Paddr InvalidPaddr = -1;

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

    static size_t defaultClusterLen; //TODO: populate from cmdline
    static size_t defaultReadaheadLen;

    //NOTE: assumes range.mutex is is held.
    static ClusterWindow ChooseClusterWindow(VmRange& range, size_t slot,
        bool write)
    {
        (void)write;

        size_t target = defaultClusterLen;
        if (UnderMemoryPressure())
            target = 1;
        else if (range.advice == VmAdvice::Random)
            target = 1;
        else if (range.advice == VmAdvice::WillNeed)
            target = MaxClusterPages;

        const bool linear = range.lastFaultSlotPlusOne != 0
            && range.lastFaultSlotPlusOne == slot;
        range.lastFaultSlotPlusOne = slot + 1;

        ClusterWindow window {};
        if (linear || range.advice == VmAdvice::Sequential || target == 1)
            window.first = slot;
        else
        {
            const size_t back = (target - 1) / 2;

            window.first = slot - back;
            if (slot <= back)
                window.first = 0;
        }

        const size_t rangeLen = range.length >> PfnShift();
        const size_t last = sl::Min(window.first + target, rangeLen);
        window.count = last - window.first;
        window.mainIndex = slot - window.first;

        return window;
    }

    static void AnonDropSwapForWrite(AnonPage& anon, PageInfo* page)
    {
        SwapSlot stale = NoSwapSlot;

        anon.lock.Lock();
        if (anon.swapSlot != NoSwapSlot)
        {
            stale = anon.swapSlot;
            anon.swapSlot = NoSwapSlot;
            page->vm.flags.Clear(PageVmFlag::Clean);
        }
        
        page->vm.flags.Clear(PageVmFlag::Busy);
        anon.lock.Unlock();

        if (stale != NoSwapSlot)
            SwapFreeSlot(stale);
    }

    //NOTE: caller is expected to hold `range->mutex` for the range covering
    //`addr`.
    static NpkStatus DropPreviousMapping(Paddr* prev, HwMap* map,
        uintptr_t addr)
    {
        auto result = HwMapExtract(prev, nullptr, nullptr, map, addr);

        if (result == NpkStatus::BadVaddr)
            return NpkStatus::NotAvailable;
        if (result != NpkStatus::Success)
            return result;

        result = HwMapRemove(map, addr, 1);
        if (result != NpkStatus::Success)
            return result;

        PageList freeAfter {};
        HwMapUpdate(map, true, freeAfter);

        return NpkStatus::Success;
    }

    static NpkStatus EnsurePrivateAmap(VmRange& range, size_t slot)
    {
        auto CalcSlotCount = [](size_t target, size_t max) -> size_t
        {
            target++;
            target += target / 2;

            return sl::Min(target, max);
        };

        const size_t totalSlots = range.length >> PfnShift();
        const size_t targetSlots = CalcSlotCount(slot, totalSlots);

        if (!range.amapRef.Valid())
        {
            AnonMap* ptr;
            auto result = CreateAnonMap(&ptr, targetSlots);
            if (result != NpkStatus::Success)
                return result;

            result = AnonMapLinkRange(*ptr, &range);
            if (result != NpkStatus::Success)
            {
                auto doomed = AnonMapRef::Adopt(ptr);
                doomed.Release();

                return result;
            }

            range.amapOffset = 0;
            range.amapRef = AnonMapRef::Adopt(ptr);
        }
        else if (range.flags.Has(VmFlag::AmapNeedsCopy))
        {
            if (range.amapRef->refcount > 1)
            {
                AnonMapRef mapRef {};
                auto result = Private::AnonMapClone(&mapRef, *range.amapRef);
                if (result != NpkStatus::Success)
                    return result;

                NPK_ASSERT(mapRef.Valid());

                result = AnonMapUnlinkRange(*range.amapRef, &range);
                if (result != NpkStatus::Success)
                    return result;

                result = AnonMapLinkRange(*mapRef, &range);
                if (result != NpkStatus::Success)
                {
                    range.amapRef.Release();

                    return result;
                }

                range.amapRef = mapRef;
            }

            range.flags.Clear(VmFlag::AmapNeedsCopy);
        }


        if (range.amapRef->slotCount <= slot)
        {
            auto result =  ResizeAnonMap(*range.amapRef, targetSlots);
            if (result != NpkStatus::Success)
                return result;
        }

        return NpkStatus::Success;
    }
    
    static void InitVmPage(PageInfo* page, bool clean, PageVmOwnerType type,
        void* owner, size_t offset)
    {
        page->vm.pins = 0;
        page->vm.offset = offset;
        PageInfoSetVmOwner(page, owner, type);

        page->vm.flags.Reset();
        if (clean)
            page->vm.flags.Set(PageVmFlag::Clean);

        PinPage(page);
    }

    //NOTE: assumes `anon.lock` is held.
    static void PublishAnonPage(AnonPage& anon, PageInfo* page, AnonMap& amap,
        size_t slot, bool clean)
    {
        NPK_ASSERT(anon.page == nullptr);

        anon.page = page;

        InitVmPage(page, clean, PageVmOwnerType::Anon, &amap, slot);
    }

    static void ReleaseAnonPage(AnonMap& map, PageInfo* page)
    {
        if (page == nullptr)
            return;

        auto ref = AnonMapLookup(map, page->vm.offset);
        if (!ref.Valid())
        {
            //not good! This is a UAF as the page is not being tracked by the
            //amap now but we thought it was.
            Log("Attempted to release anon page %p from slot %u: empty slot",
                LogLevel::Error, page, page->vm.offset);
            
            return;
        }

        ref->lock.Lock();
        NPK_ASSERT(ref->page == page);
        UnpinPage(page);
        ref->lock.Unlock();
    }

    static void ReleasePinnedAnon(VmRange& range, PageInfo*& page)
    {
        if (page == nullptr || !range.amapRef.Valid())
            return;

        ReleaseAnonPage(*range.amapRef, page);
        page = nullptr;
    }

    static NpkStatus PopulateAnonSlot(Paddr* paddr, VmRange& range, size_t slot)
    {
        AnonPage* anon;
        auto result = CreateAnonPage(&anon);
        if (result != NpkStatus::Success)
            return result;
        auto ref = AnonPageRef::Adopt(anon);

        auto* page = AllocPage(true);
        if (page == nullptr)
            return NpkStatus::Shortage;

        anon->lock.Lock();
        PublishAnonPage(*anon, page, *range.amapRef, slot, false);
        anon->lock.Unlock();

        result = AnonMapAdd(*range.amapRef, slot, ref);
        if (result != NpkStatus::Success)
        {
            anon->lock.Lock();
            UnpinPage(page);
            anon->page = nullptr;
            anon->lock.Unlock();

            FreePage(page);

            return result;
        }

        TrackPage(page);
        *paddr = LookupPagePaddr(page);

        return NpkStatus::Success;
    }

    static NpkStatus FaultOnAnonPage(PageInfo** out, AnonPageRef ref,
        AnonMap& map, size_t slot)
    {
        ref->lock.Lock();
        const auto swapSlot = ref->swapSlot;
        const bool isResident = ref->page != nullptr;
        ref->lock.Unlock();

        if (isResident || swapSlot == NoSwapSlot)
            return NpkStatus::NotAvailable;

        auto* page = AllocPage(true);
        if (page == nullptr)
            return NpkStatus::Shortage;

        size_t pagesWanted = defaultReadaheadLen;
        if (UnderMemoryPressure())
            pagesWanted = 1;
        
        PageInfo* pages[MaxSwapClusterPages] {};
        AnonPageRef aheadRefs[MaxSwapClusterPages] {};
        SwapSlot aheadSlots[MaxSwapClusterPages] {};
        size_t count = 1;

        pages[0] = page;
        aheadSlots[0] = swapSlot;

        SwapSlot prev = swapSlot;
        while (count < pagesWanted)
        {
            auto next = AnonMapLookup(map, slot + count);
            if (!next.Valid())
                break;

            next->lock.Lock();
            const auto nextSlot = next->swapSlot;
            const bool nextResident = next->page != nullptr;
            next->lock.Unlock();

            if (nextResident || !SwapSlotsAdjacent(prev, nextSlot))
                break;

            auto* nextPage = AllocPage(true);
            if (nextPage == nullptr)
                break;

            pages[count] = nextPage;
            aheadRefs[count] = next;
            aheadSlots[count] = nextSlot;

            prev = nextSlot;
            count++;
        }

        SwapOp op {};
        auto result = SwapBeginRead(op, swapSlot, { pages, count });
        if (result != NpkStatus::Success && result != NpkStatus::Pending)
        {
            for (size_t i = 0; i < count; i++)
                FreePage(pages[i]);

            return result;
        }
        result = SwapEndRead(op);
        if (result != NpkStatus::Success)
        {
            for (size_t i = 0; i < count; i++)
                FreePage(pages[i]);

            return result;
        }

        for (size_t i = 1; i < count; i++)
        {
            auto& aheadRef = *aheadRefs[i];

            aheadRef.lock.Lock();
            if (aheadRef.page != nullptr || aheadRef.swapSlot != aheadSlots[i])
            {
                aheadRef.lock.Unlock();
                FreePage(pages[i]);

                continue;
            }

            PublishAnonPage(aheadRef, pages[i], map, slot + i, true);
            UnpinPage(pages[i]);
            aheadRef.lock.Unlock();

            TrackPage(pages[i]);
        }

        ref->lock.Lock();
        if (ref->page != nullptr)
        {
            //race handling: someone else paged in the page's data before us
            //and attached it to the anon. Discard our copy and return theirs.

            auto* winner = ref->page;
            PinPage(winner);
            ref->lock.Unlock();

            FreePage(page);
            *out = winner;

            return NpkStatus::Success;
        }

        PublishAnonPage(*ref, page, map, slot, true);
        ref->lock.Unlock();

        TrackPage(page);
        *out = page;

        return NpkStatus::Success;
    }

    static void ReleaseSourcePage(VmSource& src, PageInfo* page)
    {
        if (page == nullptr)
            return;

        if (AcquireSxMutexExclusive(&src.mutex, sl::NoTimeout,
            NPK_WAIT_LOCATION) != NpkStatus::Success)
            return;

        UnpinPage(page);
        ReleaseSxMutexExclusive(&src.mutex);
    }

    static void ReleaseSourceCluster(SourceFaultResult& fetched, VmSource* src)
    {
        if (src == nullptr)
            return;

        for (size_t i = 0; i < fetched.count; i++)
        {
            auto& page = fetched.pages[i];

            if (page == nullptr)
                continue;
            ReleaseSourcePage(*src, page);
            page = nullptr;
        }
    }

    static NpkStatus ReleaseReplacedPage(Paddr prevPaddr, Paddr newPaddr)
    {
        //the same page with wider permissions: nothing changed hands.
        if (prevPaddr == newPaddr)
            return NpkStatus::Success;

        return ReleaseMappedPage(prevPaddr);
    }

    //NOTE: assumes range.mutex is held and the amap is private.
    static NpkStatus CopySourcePageToAmap(Paddr* paddr, VmRange& range,
        size_t slot, PageInfo* sourcePage)
    {
        AnonPage* anon;
        auto result = CreateAnonPage(&anon);
        if (result != NpkStatus::Success)
            return result;

        auto ref = AnonPageRef::Adopt(anon);
        auto page = AllocPage(true);
        if (page == nullptr)
            return NpkStatus::Shortage;

        const auto prevIpl = EnsureIpl(Ipl::Dpc); //prevent preemption
        auto dest = AccessPage(page);
        auto src = AccessPage(sourcePage);

        if (!dest.vaddr || !src.Valid())
        {
            RestoreIpl(prevIpl);
            FreePage(page);

            return NpkStatus::Shortage;
        }

        sl::MemCopy(dest.vaddr, src.vaddr, PageSize());

        DestroyPageAccess(&dest);
        DestroyPageAccess(&src);
        RestoreIpl(prevIpl);

        anon->lock.Lock();
        PublishAnonPage(*anon, page, *range.amapRef, slot, false);
        anon->lock.Unlock();

        result = AnonMapAdd(*range.amapRef, slot, ref);
        if (result != NpkStatus::Success)
        {
            anon->lock.Lock();
            UnpinPage(page);
            anon->page = nullptr;
            anon->lock.Unlock();

            FreePage(page);

            return result;
        }

        TrackPage(page);
        *paddr = LookupPagePaddr(page);

        return NpkStatus::Success;
    }

    //NOTE: assumes `src.mutex` is held in any mode.
    static PageInfo* SourceCacheLookup(VmSource& src, size_t index)
    {
        for (auto it = src.pages.Begin(); it != src.pages.End(); ++it)
        {
            if (it->vm.offset == index)
                return &*it;
        }

        return nullptr;
    }

    //NOTE: assumes `src.mutex` is held exclusively.
    static PageInfo* PublishSourcePage(VmSource& src, PageInfo* page,
        size_t index)
    {
        auto* found = SourceCacheLookup(src, index);
        if (found != nullptr)
        {
            FreePage(page);
            PinPage(found);

            return found;
        }

        InitVmPage(page, true, PageVmOwnerType::Source, &src, index);
        src.pages.PushFront(page);

        return page;
    }

    //NOTE: assumes `range.mutex` is held.
    static NpkStatus FaultOnSource(SourceFaultResult& out, VmRange& range,
        size_t slot, bool write)
    {
        auto& src = *range.source;
        const auto window = ChooseClusterWindow(range, slot, write);
        const size_t baseIndex = (range.offset >> PfnShift()) + window.first;
        const size_t first = window.first;

        out.count = window.count;
        out.mainIndex = window.mainIndex;
        out.page0Addr = range.base + (first << PfnShift());

        PageInfo* gotPages[MaxClusterPages] {};
        bool publishedPages[MaxClusterPages] {};
        bool anyPagesNeedGet = false;

        auto result = AcquireSxMutexShared(&src.mutex, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        if (result != NpkStatus::Success)
            return result;

        for (size_t i = 0; i < out.count; i++)
        {
            auto found = SourceCacheLookup(src, baseIndex + i);
            
            out.pages[i] = found;
            if (found == nullptr)
                anyPagesNeedGet = true;
        }
        ReleaseSxMutexShared(&src.mutex);

        NpkStatus pagerResult = NpkStatus::NotAvailable;
        if (anyPagesNeedGet)
        {
            for (size_t i = 0; i < out.count; i++)
            {
                if (out.pages[i] != nullptr)
                    continue;

                gotPages[i] = AllocPage(true);
                if (gotPages[i] == nullptr && i == out.mainIndex)
                {
                    for (size_t j = 0; j < i; j++)
                    {
                        if (gotPages[j] != nullptr)
                            FreePage(gotPages[j]);
                    }

                    return NpkStatus::Shortage;
                }

                if (gotPages[i] != nullptr)
                    gotPages[i]->vm.flags.Clear(PageVmFlag::Filled);
            }

            PagerFlags flags {};
            if (write && !range.flags.Has(VmFlag::CopyOnWrite))
                flags.Set(PagerFlag::Write);

            pagerResult = src.ops->Get(&src, { gotPages, out.count },
                baseIndex, out.mainIndex, flags);
        }

        result = AcquireSxMutexExclusive(&src.mutex, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        if (result != NpkStatus::Success)
        {
            for (size_t i = 0; i < out.count; i++)
            {
                if (gotPages[i] != nullptr)
                    FreePage(gotPages[i]);
            }

            return result;
        }

        for (size_t i = 0; i < out.count; i++)
        {
            if (gotPages[i] == nullptr)
            {
                if (out.pages[i] != nullptr)
                    PinPage(out.pages[i]);
                continue;
            }

            if (!gotPages[i]->vm.flags.Has(PageVmFlag::Filled))
            {
                FreePage(gotPages[i]);
                out.pages[i] = nullptr;

                continue;
            }

            out.pages[i] = PublishSourcePage(src, gotPages[i], baseIndex + i);
            publishedPages[i] = out.pages[i] == gotPages[i];
        }
        ReleaseSxMutexExclusive(&src.mutex);

        for (size_t i = 0; i < out.count; i++)
        {
            if (publishedPages[i])
                TrackPage(out.pages[i]);
        }

        if (out.pages[out.mainIndex] == nullptr)
        {
            ReleaseSourceCluster(out, &src);
            NPK_ASSERT(pagerResult != NpkStatus::Success);

            return pagerResult;
        }

        return NpkStatus::Success;
    }

    //NOTE: assumes `space->rangesMutex` is held in some form.
    static NpkStatus AttemptPageFaultLocked(VmSpace& space, uintptr_t addr,
        bool write)
    {
        VmRange* range;
        auto result = SpaceLookupLocked(&range, space, addr, 1);
        if (result != NpkStatus::Success)
            return result;

        result = AcquireMutex(&range->mutex, sl::NoTimeout, NPK_WAIT_LOCATION);
        if (result != NpkStatus::Success)
            return result;

        if (write && !range->flags.Has(VmFlag::Write))
        {
            ReleaseMutex(&range->mutex);

            return NpkStatus::NotWritable;
        }

        NPK_ASSERT(range->amapOffset == 0);
        const size_t slot = range->amapOffset + ((addr - range->base)
            >> PfnShift());
        const bool isCow = range->flags.Has(VmFlag::CopyOnWrite);

        Paddr paddr {};
        bool resolved = false;
        PageInfo* pinnedAnon = nullptr;

        if (write && isCow)
        {
            result = EnsurePrivateAmap(*range, slot);
            if (result != NpkStatus::Success)
            {
                ReleaseMutex(&range->mutex);

                return result;
            }
        }

        //NOTE: only runs once, a `while` is used for short circuiting with
        //`break` below.
        while (range->amapRef.Valid())
        {
            auto ref = AnonMapLookup(*range->amapRef, slot);
            if (!ref.Valid())
                break; //amap doesnt cover this page

            PageInfo* page;
            result = AnonPageGetPage(&page, ref);
            if (result == NpkStatus::Success)
            {
                pinnedAnon = page;
                paddr = LookupPagePaddr(page);
                resolved = true;

                if (write)
                    AnonDropSwapForWrite(*ref, page);
            }
            else if (result == NpkStatus::NotAvailable) //not resident
            {
                result = FaultOnAnonPage(&page, ref, *range->amapRef, slot);
                if (result == NpkStatus::Success)
                {
                    pinnedAnon = page;
                    paddr = LookupPagePaddr(page);
                    resolved = true;

                    if (write)
                        AnonDropSwapForWrite(*ref, page);
                }
                else if (result != NpkStatus::NotAvailable)
                {
                    ReleaseMutex(&range->mutex);

                    return result;
                }
            }
            else //real error
            {
                ReleaseMutex(&range->mutex);

                return result;
            }

            break;
        }

        SourceFaultResult srcResult {};
        bool haveSrcResult = false;
        if (!resolved && range->source != nullptr)
        {
            result = FaultOnSource(srcResult, *range, slot, write);
            if (result != NpkStatus::Success)
            {
                ReleaseMutex(&range->mutex);

                return result;
            }
            haveSrcResult = true;

            auto* mainPage =  srcResult.pages[srcResult.mainIndex];
            if (write && isCow)
            {
                result = CopySourcePageToAmap(&paddr, *range, slot, mainPage);
                if (result != NpkStatus::Success)
                {
                    ReleaseMutex(&range->mutex);

                    return result;
                }

                pinnedAnon = LookupPageInfo(paddr);

                //amap contains the page we want to map now, so clear it from
                //the array provided by the source and unpin *that* page.
                srcResult.pages[srcResult.mainIndex] = nullptr;
                ReleaseSourcePage(*range->source, mainPage);
            }
            else
                paddr = LookupPagePaddr(mainPage);

            resolved = true;
        }

        if (!resolved && isCow)
        {
            //amap and source layers had nothing, so its a zero fill demand
            //allocate region.

            if (write)
            {
                result = PopulateAnonSlot(&paddr, *range, slot);
                if (result != NpkStatus::Success)
                {
                    ReleaseMutex(&range->mutex);

                    return result;
                }

                pinnedAnon = LookupPageInfo(paddr);
            }
            else
                paddr = MySystemDomain().zeroPage;
            resolved = true;
        }

        if (!resolved)
        {
            //amap, source and not marked as CoW, this is as far as we go.
            ReleaseMutex(&range->mutex);

            return NpkStatus::BadVaddr;
        }

        //we have what we need to map, now to unmap anything at this location.
        Paddr prevPaddr = InvalidPaddr;
        result = DropPreviousMapping(&prevPaddr, space.map, addr);
        if (result != NpkStatus::Success && result != NpkStatus::NotAvailable)
        {
            ReleaseSourceCluster(srcResult,
                haveSrcResult ? range->source : nullptr);
            ReleasePinnedAnon(*range, pinnedAnon);
            ReleaseMutex(&range->mutex);

            return result;
        }

        if (result == NpkStatus::Success)
        {
            if (prevPaddr == paddr)
            {
                ReleasePinnedAnon(*range, pinnedAnon);

                if (haveSrcResult && range->source != nullptr)
                {
                    auto* mainPage = srcResult.pages[srcResult.mainIndex];

                    srcResult.pages[srcResult.mainIndex] = nullptr;
                    ReleaseSourcePage(*range->source, mainPage);
                }
            }

            result = ReleaseReplacedPage(prevPaddr, paddr);
        }

        if (result != NpkStatus::Success && result != NpkStatus::NotAvailable)
        {
            ReleaseSourceCluster(srcResult,
                haveSrcResult ? range->source : nullptr);
            ReleasePinnedAnon(*range, pinnedAnon);
            ReleaseMutex(&range->mutex);

            return result;
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

        //prepare and actually add the new mapping in response to the fault!
        MmuPermissions perms {};
        MmuCacheMode mode {};
        ConvertFlags(perms, mode, range->flags, write);

        result = HwMapAdd(space.map, addr, paddr, perms, mode, false);
        if (result != NpkStatus::Success)
        {
            ReleaseSourceCluster(srcResult,
                haveSrcResult ? range->source : nullptr);
            ReleasePinnedAnon(*range, pinnedAnon);
            ReleaseMutex(&range->mutex);

            return result;
        }

        //bonus round: map clustered pages from the source layer
        if (haveSrcResult)
        {
            //clustered pages are always mapped readonly and never overwrite
            //an existing mapping.
            ConvertFlags(perms, mode, range->flags, false);

            for (size_t i = 0; i < srcResult.count; i++)
            {
                if (i == srcResult.mainIndex)
                    continue;

                auto* page = srcResult.pages[i];
                if (page == nullptr)
                    continue;

                const uintptr_t at = srcResult.page0Addr + (i << PfnShift());
                Paddr existing;
                result = HwMapExtract(&existing, nullptr,
                    nullptr, space.map, at);

                const bool occupied = result == NpkStatus::Success;
                if (!occupied)
                {
                    result = HwMapAdd(space.map, at, LookupPagePaddr(page),
                        perms, mode, false);
                }
                
                if (occupied || result != NpkStatus::Success)
                    ReleaseSourcePage(*range->source, page);
            }
        }

        ReleaseMutex(&range->mutex);

        return NpkStatus::Success;
    }

    static NpkStatus AttemptPageFault(VmSpace& space, uintptr_t addr,
        bool write)
    {
        auto result = AcquireSxMutexShared(&space.rangesMutex, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        if (result != NpkStatus::Success)
            return result;

        result = AttemptPageFaultLocked(space, addr, write);
        ReleaseSxMutexShared(&space.rangesMutex);

        return result;
    }

    void Private::DrainSourcePages(VmSource& src)
    {
        NPK_ASSERT(src.ranges.Empty());

        //this lock also synchronizes with the trimmer looking at this source
        //from the perspective of a PageInfo's owner.
        NPK_ASSERT(AcquireSxMutexExclusive(&src.mutex, sl::NoTimeout,
            NPK_WAIT_LOCATION) == NpkStatus::Success);

        while (!src.pages.Empty())
        {
            auto* page = src.pages.PopFront();

            UntrackPage(page);

            NPK_ASSERT(page->vm.pins == 0);
            FreePage(page);
        }

        ReleaseSxMutexExclusive(&src.mutex);
    }

    NpkStatus ReleaseMappedPage(Paddr paddr)
    {
        if (paddr == MySystemDomain().zeroPage)
            return NpkStatus::Success;
        if (!PaddrHasPageInfo(paddr))
            return NpkStatus::Unsupported;

        auto* page = LookupPageInfo(paddr);
        PageVmOwnerType type;
        void* owner = PageInfoGetVmOwner(type, page);
        if (owner == nullptr)
            return NpkStatus::Success;

        switch (type)
        {
        case PageVmOwnerType::Source:
            ReleaseSourcePage(*static_cast<VmSource*>(owner), page);
            return NpkStatus::Success;

        case PageVmOwnerType::Anon:
            ReleaseAnonPage(*static_cast<AnonMap*>(owner), page);
            return NpkStatus::Success;

        default:
            NPK_UNREACHABLE();
        }
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
