#include <private/Vm.hpp>
#include <lib/Maths.hpp>
#include <lib/Units.hpp>

namespace Npk
{
    constexpr HeapTag SpaceHeapTag = NPK_MAKE_HEAP_TAG("Spac");

    constexpr size_t CloneProtectRanges = 32;

    static void FreeRangeNow(EbrItem* item)
    {
        PoolFreeWired(reinterpret_cast<VmRange*>(item), sizeof(VmRange),
            SpaceHeapTag);
    }

    //a range that was in the tree: a lockless walk may still stand on it.
    static void RetireRange(VmRange* range)
    {
        range->retire.callback = FreeRangeNow;
        const auto result = RcuCall(&range->retire);
        NPK_ASSERT(result == NpkStatus::Success);
    }

    static bool RangeDying(const VmRange& range)
    {
        return (range.faultRefs.Load(sl::Acquire) & Private::VmRangeDying) != 0;
    }

    //refused once a detach owns the range.
    static bool TryRefRange(VmRange& range)
    {
        while (true)
        {
            auto refs = range.faultRefs.Load(sl::Relaxed);

            if ((refs & Private::VmRangeDying) != 0)
                return false;
            if (range.faultRefs.CompareExchange(refs, refs + 1, sl::Acquire))
                return true;
        }
    }

    //NOTE: assumes `space.rangesMutex` is held in either mode. Any range
    //overlapping the span, dying or not: what an attach may not land on.
    static VmRange* FindOverlapLocked(VmSpace& space, uintptr_t addr,
        size_t length)
    {
        const uintptr_t end = AlignUpPage(addr + length);
        addr = AlignDownPage(addr);

        auto scan = space.ranges.GetRoot();
        while (scan != nullptr)
        {
            const uintptr_t scanEnd = scan->base
                + scan->length.Load(sl::Relaxed);
            if (addr < scanEnd && scan->base < end)
                return scan;

            if (addr < scan->base)
                scan = space.ranges.GetLeft(scan);
            else
                scan = space.ranges.GetRight(scan);
        }

        return nullptr;
    }

    static bool MarkRangeDying(VmRange& range)
    {
        auto result = AcquireMutex(&range.mutex, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        NPK_ASSERT(result == NpkStatus::Success);

        result = ResetCondition(&range.drained, 1);
        NPK_ASSERT(result == NpkStatus::Success);

        range.version++;
        const uint32_t prev = range.faultRefs.FetchOr(Private::VmRangeDying,
            sl::AcqRel);
        NPK_ASSERT((prev & Private::VmRangeDying) == 0);

        ReleaseMutex(&range.mutex);

        return prev != 0;
    }

    static void DrainRange(VmRange& range, bool busy)
    {
        if (!busy)
            return;

        WaitEntry entry {};
        const auto result = WaitOne(&range.drained, &entry, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        NPK_ASSERT(result == NpkStatus::Success);
    }

    //NOTE: assumes the space ranges mutex is held exclusive, and range is
    //dying and drained. Retires `range`.
    static void DetachLocked(VmSpace& space, VmRange* range)
    {
        NPK_ASSERT(range->faultRefs.Load(sl::Acquire) == Private::VmRangeDying);

        const uintptr_t base = range->base;
        const size_t length = range->length.Load(sl::Relaxed);

        Private::BeginMappingRemovals(*range);
        space.ranges.Remove(range);

        /* TODO: issue once after the loop, since only `EndMappingRemovals()`
         * has to follow the invalidation. Until then each pass covers the page
         * it removed, plus the slack an emptied page table needs.
         */
        constexpr size_t DetachRanges = 1 + MapBatchTableSlack;

        if (space.map != nullptr)
        {
            MapBatchRange rangeStore[DetachRanges];
            MapBatch batch {};
            ResetMapBatch(batch, space.map, { rangeStore, DetachRanges });

            for (uintptr_t at = base; at < base + length; at += PageSize())
            {
                Paddr present;

                if (HwMapExtract(&present, nullptr, nullptr, space.map, at)
                    != NpkStatus::Success)
                    continue;

                const auto harvest = HwMapHarvest(batch, at, present,
                    HwHarvestAction::Remove, {});
                if (!harvest.removed)
                    continue;

                HwMapUpdate(batch, true);
                Private::SettleMappedPage(present,
                    harvest.state.Has(MmuPageState::Dirty));
            }

            FinishMapBatch(batch);
        }

        Private::EndMappingRemovals(*range);

        if (range->amapRef.Valid())
        {
            Private::BookDetachRange(range->amapRef->book, range->upperLink,
                Private::ReleaseRemovedPage, nullptr);
            range->amapRef.Release();
        }

        if (range->source != nullptr)
        {
            Private::UnlinkLayer(range->source->book, range->lowerLink);
            range->source->ops->UnrefObj(range->source);
            range->source = nullptr;
        }

        RetireRange(range);
    }

    static void BreakdownRange(VmFreeRangeTree& tree, VmFreeRange* range, 
        uintptr_t base, size_t len, VmFreeRange** spare)
    {
        const uintptr_t rangeTop = range->base + range->length;

        tree.Remove(range);
        base = sl::Max(range->base, base);

        if (base == range->base)
        {
            range->base += len;
            range->length -= len;
            if (range->length == 0)
                PoolFreeWired(range, sizeof(*range), SpaceHeapTag);
            else
                tree.Insert(range);

            return;
        }
        else if (base + len == range->base + range->length)
        {
            range->length -= len;
            if (range->length == 0)
                PoolFreeWired(range, sizeof(*range), SpaceHeapTag);
            else
                tree.Insert(range);

            return;
        }

        auto latest = *spare;
        *spare = nullptr;

        latest->base = range->base;
        latest->length = base - latest->base;
        latest->largestChild = latest->length;
        tree.Insert(latest);

        range->base = base + len;
        range->length = rangeTop - range->base;
        range->largestChild = range->length;
        tree.Insert(range);
    }

    Private::BookSpan Private::BookLinkSpan(Book& book,
        const BookToRangeLink& link)
    {
        const VmRange* range = link.range;
        const size_t count = range->length.Load(sl::Relaxed) >> PfnShift();

        size_t first;
        if (&link == &range->upperLink)
            first = range->amapOffset;
        else
            first = range->offset >> PfnShift();

        return MakeBookSpan(book, first, first + count);
    }

    void Private::LinkLayer(Book& book, BookToRangeLink& link, VmRange* range)
    {
        link.range = range;

        auto result = AcquireSxMutexExclusive(&book.mutex, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        NPK_ASSERT(result == NpkStatus::Success);

        BookLinkRange(book, link);
        ReleaseSxMutexExclusive(&book.mutex);
    }

    void Private::UnlinkLayer(Book& book, BookToRangeLink& link)
    {
        auto result = AcquireSxMutexExclusive(&book.mutex, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        NPK_ASSERT(result == NpkStatus::Success);

        BookUnlinkRange(book, link);
        ReleaseSxMutexExclusive(&book.mutex);
    }

    NpkStatus Private::BeginFaultInRange(VmRange** out, VmSpace& space,
        uintptr_t addr)
    {
        auto result = AcquireSxMutexShared(&space.rangesMutex, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        if (result != NpkStatus::Success)
            return result;

        result = NpkStatus::BadVaddr;

        VmRange* found = FindOverlapLocked(space, addr, 1);
        if (found != nullptr && TryRefRange(*found))
        {
            *out = found;
            result = NpkStatus::Success;
        }
        ReleaseSxMutexShared(&space.rangesMutex);

        return result;
    }

    void Private::EndFaultInRange(VmRange& range)
    {
        const uint32_t prev = range.faultRefs.FetchSub(1, sl::AcqRel);
        NPK_ASSERT((prev & ~Private::VmRangeDying) != 0);

        if (prev == (Private::VmRangeDying | 1))
            SetCondition(&range.drained);
    }

    void Private::WaitForFaultGateOfSpace(VmSpace& space)
    {
        if (!space.faultGate.Load(sl::Acquire))
            return;

        //the clone holds this exclusive until after it lowers the gate
        const auto result = AcquireSxMutexShared(&space.rangesMutex,
            sl::NoTimeout, NPK_WAIT_LOCATION);
        NPK_ASSERT(result == NpkStatus::Success);
        ReleaseSxMutexShared(&space.rangesMutex);
    }

    bool VmFreeRangeAggregator::Aggregate(VmFreeRange* range)
    {
        size_t length = range->length;
        auto left = VmFreeRangeTree::GetLeft(range);
        auto right = VmFreeRangeTree::GetRight(range);

        if (left != nullptr && left->largestChild > length)
            length = left->largestChild;
        if (right != nullptr && right->largestChild > length)
            length = right->largestChild;

        if (length == range->largestChild)
            return false;

        range->largestChild = length;
        return true;
    }

    void InitKernelVmSpace(uintptr_t lowBase, size_t lowLen, uintptr_t highBase,
        size_t highLen)
    {
        NPK_ASSERT((lowBase & PageMask()) == 0);
        NPK_ASSERT((highBase & PageMask()) == 0);

        //1. Initialize kernel pool allocators, these are our general purpose
        //heaps, one wired and one pageable. The wired heap is necessary for
        //use by vmm structures accessed by pager functions, the pageable heap
        //is for everything else.
        const uintptr_t poolBase = lowBase;
        const size_t poolSize = AlignDownPage(lowLen / 4);
        Private::InitPool(poolBase, poolSize);

        auto conv = sl::ConvertUnits(poolSize);
        Log("Pool space: 0x%tx-0x%tx (%zu.%03zu %sB)",
            LogLevel::Verbose, poolBase, poolBase + poolSize,
            conv.major, conv.minor, conv.prefix);

        //2. TODO: implement csegs and explain why this is useful/how it works.
        const uintptr_t cacheBase = poolBase + poolSize;
        const size_t cacheSize = AlignDownPage(lowLen / 4);

        conv = sl::ConvertUnits(cacheSize);
        Log("Cache space: 0x%tx-0x%tx (%zu.%03zu %sB)",
            LogLevel::Verbose, cacheBase, cacheBase + cacheSize,
            conv.major, conv.minor, conv.prefix);

        const uintptr_t systemLowBase = cacheBase + cacheSize;
        const size_t systemLowSize = AlignDownPage(lowLen / 2);

        //3. Initialize general virtual address space manager for kernel. This
        //is sometimes referred to as "system space" (a borrowed term). 
        //These virtual addresses are managed in the same way as userspace
        //addresses are, with all the perks and caveats that come with that.
        void* ptr = PoolAllocWired(sizeof(VmSpace), SpaceHeapTag);
        NPK_ASSERT(ptr != nullptr);
        MySystemDomain().kernelSpace = new(ptr) VmSpace {};
        auto mySpace = MySystemDomain().kernelSpace;
        mySpace->map = MySystemDomain().kernelMap;

        auto result = ResetMutex(&MySystemDomain().liveLists.lock, 1);
        NPK_ASSERT(result == NpkStatus::Success);

        ResetMutex(&mySpace->freeRangesMutex, 1);
        ResetSxMutex(&mySpace->rangesMutex);

        ptr = PoolAllocWired(sizeof(VmFreeRange), SpaceHeapTag);
        NPK_ASSERT(ptr != nullptr);

        auto* range = new(ptr) VmFreeRange {};
        range->base = systemLowBase;
        range->length = systemLowSize;
        mySpace->freeRanges.Insert(range);

        ptr = PoolAllocWired(sizeof(VmFreeRange), SpaceHeapTag);
        NPK_ASSERT(ptr != nullptr);

        range = new(ptr) VmFreeRange {};
        range->base = highBase;
        range->length = highLen;
        mySpace->freeRanges.Insert(range);

        conv = sl::ConvertUnits(systemLowSize);
        Log("General space (low): 0x%tx-0x%tx (%zu.%03zu %sB)",
            LogLevel::Verbose, systemLowBase, systemLowBase + systemLowSize,
            conv.major, conv.minor, conv.prefix);

        conv = sl::ConvertUnits(highLen);
        Log("General space (high): 0x%tx-0x%tx (%zu.%03zu %sB)",
            LogLevel::Verbose, highBase, highBase + highLen,
            conv.major, conv.minor, conv.prefix);
    }

    NpkStatus SpaceAlloc(VmSpace& space, uintptr_t* addr, size_t length,
        AllocConstraints constr)
    {
        if (constr.alignment < 1)
            constr.alignment = 1;

        if (length == 0)
            return NpkStatus::InvalidArg;
        if ((constr.preferredAddr & PageMask()) != 0)
            return NpkStatus::InvalidArg;
        if (constr.alignment != 1 && !sl::IsPowerOfTwo(constr.alignment))
            return NpkStatus::InvalidArg;
        if (constr.alignment != 1 && constr.preferredAddr != 0
            && (constr.preferredAddr & (constr.alignment - 1)) != 0)
            return NpkStatus::InvalidArg;

        const bool constrained = constr.minAddr != 0
            || constr.maxAddr != static_cast<uintptr_t>(-1)
            || constr.alignment != 1;

        length = AlignUpPage(length);
        constr.minAddr = AlignUpPage(constr.minAddr);
        constr.maxAddr = AlignDownPage(constr.maxAddr);

        if (constr.minAddr > constr.maxAddr)
            return NpkStatus::InvalidArg;

        //The worst case below is that we allocate in the middle of a range.
        //We can re-use the existing range to represent the space before, but
        //we'll need another range to represent the space after. I dont want
        //to nest mutex holding (we hold the address space mutex for most of
        //this function) so allocate the spare free range struct here.
        //If its used we'll set this pointer to null, and when returning if
        //this pointer is non-null we'll just free this memory.
        //A bit wasteful, but I think its better than the alternative.
        void* sparePtr = PoolAllocWired(sizeof(VmFreeRange), SpaceHeapTag);
        if (sparePtr == nullptr)
            return NpkStatus::Shortage;
        VmFreeRange* spareRange = new(sparePtr) VmFreeRange{};

        auto result = AcquireMutex(&space.freeRangesMutex, constr.timeout, 
            NPK_WAIT_LOCATION);
        if (result != NpkStatus::Success)
        {
            PoolFreeWired(spareRange, sizeof(*spareRange), SpaceHeapTag);

            return result;
        }

        if (constr.preferredAddr != 0)
        {
            //caller has a preferred address, try allocate there
            auto scan = space.freeRanges.GetRoot();
            while (scan != nullptr)
            {
                if (constr.preferredAddr < scan->base)
                    scan = VmFreeRangeTree::GetLeft(scan);
                else if (constr.preferredAddr >= scan->base + scan->length)
                    scan = VmFreeRangeTree::GetRight(scan);
                else if (constr.preferredAddr >= scan->base)
                    break;
                else
                    NPK_UNREACHABLE();
            }

            //preferredAddr is within `scan`, how about the length?
            if (scan != nullptr 
                && constr.preferredAddr - scan->base + length > scan->length)
                scan = nullptr;

            if (scan != nullptr)
            {
                //preferred address is available.
                BreakdownRange(space.freeRanges, scan, constr.preferredAddr, 
                    length, &spareRange);
                ReleaseMutex(&space.freeRangesMutex);

                *addr = constr.preferredAddr;
                if (spareRange != nullptr)
                {
                    PoolFreeWired(spareRange, sizeof(*spareRange),
                        SpaceHeapTag);
                }

                return NpkStatus::Success;
            }
            else if (constr.hardPreference)
            {
                //failed to allocate and its a hard requirement
                ReleaseMutex(&space.freeRangesMutex);
                PoolFreeWired(spareRange, sizeof(*spareRange), SpaceHeapTag);

                return NpkStatus::InUse;
            }
            //else: not a hard requirement, try normal allocation path
        }

        uintptr_t candidateAddr = 0;
        VmFreeRange* scan = nullptr;
        if (!constrained)
        {
            //no constraints = fast path

            scan = space.freeRanges.GetRoot();
            while (scan != nullptr)
            {
                auto left = space.freeRanges.GetLeft(scan);
                auto right = space.freeRanges.GetRight(scan);
                auto first = constr.topDown ? right : left;
                auto last = constr.topDown ? left : right;

                if (first != nullptr && first->largestChild >= length)
                {
                    scan = first;
                    continue;
                }

                if (scan->length >= length)
                {
                    candidateAddr = scan->base;
                    if (constr.topDown)
                        candidateAddr += scan->length - length;
                    break;
                }

                if (last != nullptr && last->largestChild >= length)
                {
                    scan = last;
                    continue;
                }

                //no nodes large enough, gtfo
                scan = nullptr;
                break;
            }
        }
        else
        {
            //caller is picky so we'll degrade the tree into a flat list and
            //search for a space that satisfies the requirements.

            auto CanSatisfy = [=](const VmFreeRange* range, bool topDown,
                uintptr_t* addr) -> bool
            {
                if (range->length < length)
                    return false;

                uintptr_t low = sl::Max(range->base, constr.minAddr);
                uintptr_t high = range->base + range->length;

                if (low >= high)
                    return false;
                if (constr.maxAddr < length - 1)
                    return false;

                const uintptr_t maxStart = constr.maxAddr - (length - 1);
                if (!topDown)
                {
                    if (constr.alignment != 1)
                        low = sl::AlignUp(low, constr.alignment);
                    if (low > maxStart || low + length > high)
                        return false;

                    *addr = low;
                }
                else
                {
                    uintptr_t topBound = sl::Min(high - length, maxStart);

                    if (topBound < low)
                        return false;
                    if (constr.alignment != 1)
                        topBound = sl::AlignDown(topBound, constr.alignment);
                    if (topBound < low)
                        return false;

                    *addr = topBound;
                }

                return true;
            };

            if (!constr.topDown)
            {
                for (auto it = space.freeRanges.First(); it != nullptr;
                    it = VmFreeRangeTree::Successor(it))
                {
                    if (!CanSatisfy(it, false, &candidateAddr))
                        continue;

                    scan = it;
                    break;
                }
            }
            else
            {
                auto begin = space.freeRanges.GetRoot();
                while (begin != nullptr
                    && VmFreeRangeTree::GetRight(begin) != nullptr)
                    begin = VmFreeRangeTree::GetRight(begin);

                for (auto it = begin; it != nullptr; 
                    it = VmFreeRangeTree::Predecessor(it))
                {
                    if (!CanSatisfy(it, true, &candidateAddr))
                        continue;

                    scan = it;
                    break;
                }
            }
        }

        NpkStatus status = NpkStatus::Shortage;
        if (scan != nullptr)
        {
            status = NpkStatus::Success;
            *addr = candidateAddr;
            BreakdownRange(space.freeRanges, scan, candidateAddr, length, 
                &spareRange);
        }

        ReleaseMutex(&space.freeRangesMutex);
        if (spareRange != nullptr)
            PoolFreeWired(spareRange, sizeof(*spareRange), SpaceHeapTag);

        return status;
    }

    NpkStatus SpaceFree(VmSpace& space, uintptr_t base, size_t length, 
        sl::TimeCount timeout)
    {
        void* sparePtr = PoolAllocWired(sizeof(VmFreeRange), SpaceHeapTag);
        if (sparePtr == nullptr)
            return NpkStatus::Shortage;
        VmFreeRange* spareRange = new(sparePtr) VmFreeRange{};

        auto result = AcquireMutex(&space.freeRangesMutex, timeout, 
            NPK_WAIT_LOCATION);
        if (result != NpkStatus::Success)
        {
            PoolFreeWired(spareRange, sizeof(*spareRange), SpaceHeapTag);

            return result;
        }

        VmFreeRange* pred = nullptr;
        VmFreeRange* succ = nullptr;
        auto scan = space.freeRanges.GetRoot();
        while (scan != nullptr)
        {
            if (base < scan->base)
            {
                if (VmFreeRangeTree::GetLeft(scan) != nullptr)
                    scan = VmFreeRangeTree::GetLeft(scan);
                else
                {
                    pred = VmFreeRangeTree::Predecessor(scan);
                    succ = scan;
                    break;
                }
            }
            else if (base >= scan->base + scan->length)
            {
                if (VmFreeRangeTree::GetRight(scan))
                    scan = VmFreeRangeTree::GetRight(scan);
                else
                {
                    pred = scan;
                    succ = VmFreeRangeTree::Successor(scan);
                    break;
                }
            }
            else
                break;
        }

        if (scan != nullptr && base >= scan->base 
            && base < scan->base + scan->length)
        {
            Log("Double free at 0x%tx in VM space %p", LogLevel::Error,
                base, &space);
            ReleaseMutex(&space.freeRangesMutex);
            PoolFreeWired(spareRange, sizeof(*spareRange), SpaceHeapTag);

            return NpkStatus::BadVaddr;
        }

        if ((pred != nullptr && pred->base + pred->length > base)
            || (succ != nullptr && succ->base < base + length))
        {
            Log("Free of overlapping range 0x%tx-0x%tx in VM space %p",
                LogLevel::Error, base, base + length, &space);
            ReleaseMutex(&space.freeRangesMutex);
            PoolFreeWired(spareRange, sizeof(*spareRange), SpaceHeapTag);

            return NpkStatus::BadVaddr;
        }

        //try coalesce with nearby free ranges, if any
        VmFreeRange* toFree = nullptr;
        VmFreeRange* latest = nullptr;
        if (succ != nullptr && succ->base == base + length)
        {
            space.freeRanges.Remove(succ);
            length += succ->length;
            latest = succ;
        }
        if (pred != nullptr && pred->base + pred->length == base)
        {
            space.freeRanges.Remove(pred);
            length += pred->length;
            base = pred->base;
            if (latest != nullptr)
                toFree = latest;
            latest = pred;
        }

        if (latest == nullptr)
        {
            latest = spareRange;
            spareRange = nullptr;
        }

        latest->base = base;
        latest->length = length;
        latest->largestChild = latest->length;
        space.freeRanges.Insert(latest);

        ReleaseMutex(&space.freeRangesMutex);

        if (toFree != nullptr)
            PoolFreeWired(toFree, sizeof(*toFree), SpaceHeapTag);
        if (spareRange != nullptr)
            PoolFreeWired(spareRange, sizeof(*spareRange), SpaceHeapTag);

        return NpkStatus::Success;
    }

    NpkStatus SpaceAttach(VmRange** range, VmSpace& space, uintptr_t base, 
        size_t length, VmSource* source, size_t srcOffset, VmFlags flags)
    {
        length = AlignUpPage(length);

        if (flags.Has(VmFlag::AmapNeedsCopy))
            return NpkStatus::InvalidArg;
        if (source != nullptr && (srcOffset & PageMask()) != 0)
            return NpkStatus::InvalidArg;

        if (!Private::BookBytesFit(0, length))
            return NpkStatus::TooLarge;
        if (source != nullptr && !Private::BookBytesFit(srcOffset, length))
            return NpkStatus::TooLarge;

        if (flags.Has(VmFlag::Fetch))
        {
            if (flags.Has(VmFlag::Write))
                return NpkStatus::InvalidArg;
            if (flags.Has(VmFlag::Mmio))
                return NpkStatus::InvalidArg;
        }

        bool allocatedVaddrs = false;
        if ((base & PageMask()) != 0)
        {
            auto result = SpaceAlloc(space, &base, length);
            if (result != NpkStatus::Success)
                return result;

            allocatedVaddrs = true;
        }

        auto result = AcquireSxMutexExclusive(&space.rangesMutex, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        if (result != NpkStatus::Success)
        {
            if (allocatedVaddrs)
                SpaceFree(space, base, length);

            return result;
        }

        if (FindOverlapLocked(space, base, length) != nullptr)
        {
            ReleaseSxMutexExclusive(&space.rangesMutex);
            if (allocatedVaddrs)
                SpaceFree(space, base, length);

            return NpkStatus::BadVaddr;
        }

        void* ptr = PoolAllocWired(sizeof(VmRange), SpaceHeapTag);
        if (ptr == nullptr)
        {
            ReleaseSxMutexExclusive(&space.rangesMutex);
            if (allocatedVaddrs)
                SpaceFree(space, base, length);

            return NpkStatus::Shortage;
        }

        auto* vmr = new(ptr) VmRange {};
        vmr->space = &space;

        if (ResetMutex(&vmr->mutex, 1) != NpkStatus::Success)
        {
            PoolFreeWired(vmr, sizeof(*vmr), SpaceHeapTag);
            ReleaseSxMutexExclusive(&space.rangesMutex);
            if (allocatedVaddrs)
                SpaceFree(space, base, length);

            return NpkStatus::Shortage;
        }

        vmr->flags = flags;
        vmr->advice = VmAdvice::Normal;
        vmr->nextFaultSlotPlusOne = 0;
        vmr->base = base;
        vmr->length.Store(length, sl::Relaxed);
        vmr->amapRef = {};
        vmr->amapOffset = 0;

        if (source != nullptr)
        {
            PagerFlags pagerFlags {};
            if (flags.Has(VmFlag::Write))
                pagerFlags.Set(PagerFlag::Write);

            if (!source->ops->RefObj(source, pagerFlags))
            {
                PoolFreeWired(vmr, sizeof(*vmr), SpaceHeapTag);
                ReleaseSxMutexExclusive(&space.rangesMutex);
                if (allocatedVaddrs)
                    SpaceFree(space, base, length);

                return NpkStatus::ObjRefFailed;
            }

            vmr->source = source;
            vmr->offset = srcOffset;
            Private::LinkLayer(source->book, vmr->lowerLink, vmr);
        }
        else
        {
            vmr->source = nullptr;
            vmr->offset = 0;
        }

        space.ranges.Insert(vmr);
        *range = vmr;

        ReleaseSxMutexExclusive(&space.rangesMutex);

        return NpkStatus::Success;
    }

    NpkStatus SpaceDetach(VmSpace& space, VmRange* range, bool freeAddresses)
    {
        NPK_ASSERT(range != nullptr);

        auto result = AcquireSxMutexExclusive(&space.rangesMutex, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        if (result != NpkStatus::Success)
            return result;

        VmRange* check = nullptr;
        result = SpaceLookupLocked(&check, space, range->base, 1);
        if (result != NpkStatus::Success || range != check)
        {
            ReleaseSxMutexExclusive(&space.rangesMutex);
            return NpkStatus::InvalidArg;
        }

        //two stages to detaching a range, the issue is a fault on another cpu
        //may be inside a pager's Get() intending to add to this range,
        //which may also fault on this space/acquire its locks.
        //So we first flag the range as dying, release the lock and wait for
        //ongoing ops, then relock and finish the detach.

        const bool busy = MarkRangeDying(*range);
        ReleaseSxMutexExclusive(&space.rangesMutex);
        DrainRange(*range, busy);

        result = AcquireSxMutexExclusive(&space.rangesMutex, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        NPK_ASSERT(result == NpkStatus::Success);

        const uintptr_t base = range->base;
        const size_t length = range->length.Load(sl::Relaxed);
        DetachLocked(space, range);
        ReleaseSxMutexExclusive(&space.rangesMutex);

        if (freeAddresses)
            return SpaceFree(space, base, length);
        else
            return NpkStatus::Success;
    }

    NpkStatus SpaceSplit(VmSpace& space, VmRange& range, size_t offset)
    {
        (void)space; (void)range; (void)offset;
        return NpkStatus::Unsupported; //TODO: not this lol
    }

    //NOTE: assumes space.rangesMutex is held (shared or exclusive)
    NpkStatus SpaceLookupLocked(VmRange** found, VmSpace& space,
        uintptr_t addr, size_t length)
    {
        //a dying range is going: to a find it is already gone
        VmRange* scan = FindOverlapLocked(space, addr, length);
        if (scan != nullptr && !RangeDying(*scan))
        {
            if (found != nullptr)
                *found = scan;
            return NpkStatus::Success;
        }

        return NpkStatus::BadVaddr;
    }

    NpkStatus SpaceLookup(VmRange** found, VmSpace& space, uintptr_t addr)
    {
        NPK_CHECK(found != nullptr, NpkStatus::InvalidArg);

        auto result = AcquireSxMutexShared(&space.rangesMutex, {}, 
            NPK_WAIT_LOCATION);
        if (result != NpkStatus::Success)
            return result;

        auto status = SpaceLookupLocked(found, space, addr, 1);
        ReleaseSxMutexShared(&space.rangesMutex);

        return status;
    }

    NpkStatus SpaceClone(VmSpace** clone, VmSpace& source)
    {
        auto result = AcquireMutex(&source.freeRangesMutex, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        if (result != NpkStatus::Success)
            return result;

        result = AcquireSxMutexExclusive(&source.rangesMutex, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        if (result != NpkStatus::Success)
        {
            ReleaseMutex(&source.freeRangesMutex);
            return result;
        }

        void* ptr = PoolAllocWired(sizeof(VmSpace), SpaceHeapTag);
        if (ptr == nullptr)
        {
            ReleaseSxMutexExclusive(&source.rangesMutex);
            ReleaseMutex(&source.freeRangesMutex);

            return NpkStatus::Shortage;
        }

        auto* newSpace = new(ptr) VmSpace {};
        result = ResetMutex(&newSpace->freeRangesMutex, 1);
        if (result == NpkStatus::Success)
            result = ResetSxMutex(&newSpace->rangesMutex);
        if (result != NpkStatus::Success)
        {
            PoolFreeWired(newSpace, sizeof(VmSpace), SpaceHeapTag);
            ReleaseSxMutexExclusive(&source.rangesMutex);
            ReleaseMutex(&source.freeRangesMutex);

            return result;
        }

        result = HwMapCreate(&newSpace->map);
        if (result != NpkStatus::Success)
        {
            PoolFreeWired(newSpace, sizeof(VmSpace), SpaceHeapTag);
            ReleaseSxMutexExclusive(&source.rangesMutex);
            ReleaseMutex(&source.freeRangesMutex);

            return result;
        }

        source.faultGate.Store(true, sl::SeqCst);
        for (auto it = source.ranges.First(); it != nullptr;
            it = source.ranges.Successor(it))
        {
            if (RangeDying(*it))
                continue;

            result = AcquireMutex(&it->mutex, sl::NoTimeout,
                NPK_WAIT_LOCATION);
            NPK_ASSERT(result == NpkStatus::Success);
            it->version++;
            ReleaseMutex(&it->mutex);
        }

        bool carryOn = true;
        NpkStatus cloneStatus = NpkStatus::Shortage;
        for (auto it = source.freeRanges.First(); it != nullptr;
            it = source.freeRanges.Successor(it))
        {
            ptr = PoolAllocWired(sizeof(VmFreeRange), SpaceHeapTag);
            if (ptr == nullptr)
            {
                carryOn = false;
                break;
            }

            auto* latest = new(ptr) VmFreeRange {};
            latest->base = it->base;
            latest->length = it->length;
            latest->largestChild = it->largestChild;

            newSpace->freeRanges.Insert(latest);
        }

        for (auto it = source.ranges.First(); it != nullptr && carryOn;
            it = source.ranges.Successor(it))
        {
            if (RangeDying(*it))
                continue;

            ptr = PoolAllocWired(sizeof(VmRange), SpaceHeapTag);
            if (ptr == nullptr)
            {
                carryOn = false;
                break;
            }

            auto* latest = new(ptr) VmRange {};
            latest->space = newSpace;
            if (ResetMutex(&latest->mutex, 1) != NpkStatus::Success)
            {
                PoolFreeWired(latest, sizeof(*latest), SpaceHeapTag);
                carryOn = false;
                break;
            }

            latest->base = it->base;
            latest->length.Store(it->length.Load(sl::Relaxed), sl::Relaxed);
            latest->flags = it->flags;
            latest->advice = it->advice;
            latest->nextFaultSlotPlusOne = 0;
            latest->offset = it->offset;
            latest->amapOffset = it->amapOffset;
            latest->amapRef = it->amapRef;
            latest->source = it->source;

            if (latest->amapRef.Valid())
            {
                NPK_ASSERT(latest->flags.Has(VmFlag::CopyOnWrite));
                latest->flags.Set(VmFlag::AmapNeedsCopy);

                Private::LinkLayer(latest->amapRef->book, latest->upperLink,
                    latest);
            }

            if (latest->source != nullptr)
            {
                auto src = latest->source;
                PagerFlags pagerFlags {};
                if (it->flags.Has(VmFlag::Write))
                    pagerFlags.Set(PagerFlag::Write);

                if (!src->ops->RefObj(src, pagerFlags))
                {
                    if (latest->amapRef.Valid())
                    {
                        Private::UnlinkLayer(latest->amapRef->book,
                            latest->upperLink);
                        latest->amapRef.Release();
                    }

                    PoolFreeWired(latest, sizeof(*latest), SpaceHeapTag);
                    cloneStatus = NpkStatus::ObjRefFailed;
                    carryOn = false;
                    break;
                }

                Private::LinkLayer(src->book, latest->lowerLink, latest);
            }

            newSpace->ranges.Insert(latest);
        }

        MapBatchRange rangeStore[CloneProtectRanges];
        MapBatch batch {};
        ResetMapBatch(batch, source.map, { rangeStore, CloneProtectRanges });

        bool protectedAny = false;
        for (auto it = source.ranges.First(); it != nullptr && carryOn;
            it = source.ranges.Successor(it))
        {
            if (RangeDying(*it))
                continue;
            if (!it->amapRef.Valid() || it->flags.Has(VmFlag::AmapNeedsCopy))
                continue;

            MmuPermissions perms {};
            if (it->flags.Has(VmFlag::Fetch))
                perms.Set(MmuPermission::Fetch);

            protectedAny = true;
            cloneStatus = HwMapProtect(batch, it->base,
                it->length.Load(sl::Relaxed) >> PfnShift(), perms);

            if (cloneStatus != NpkStatus::Success)
            {
                carryOn = false;
                break;
            }
        }

        //issued even after a failure: once a protect ran, entries may pend.
        if (protectedAny)
            HwMapUpdate(batch, true);

        FinishMapBatch(batch);

        //only now that the protections are visible on every cpu.
        for (auto it = source.ranges.First(); it != nullptr && carryOn;
            it = source.ranges.Successor(it))
        {
            if (RangeDying(*it))
                continue;
            if (!it->amapRef.Valid() || it->flags.Has(VmFlag::AmapNeedsCopy))
                continue;

            it->flags.Set(VmFlag::AmapNeedsCopy);
        }

        if (!carryOn)
        {
            while (newSpace->freeRanges.GetRoot() != nullptr)
            {
                auto range = newSpace->freeRanges.GetRoot();
                newSpace->freeRanges.Remove(range);
                PoolFreeWired(range, sizeof(*range), SpaceHeapTag);
            }

            while (newSpace->ranges.GetRoot() != nullptr)
            {
                auto range = newSpace->ranges.GetRoot();
                newSpace->ranges.Remove(range);

                //off both reverse maps before the memory goes back.
                if (range->amapRef.Valid())
                {
                    Private::UnlinkLayer(range->amapRef->book,
                        range->upperLink);
                    range->amapRef.Release();
                }

                if (range->source != nullptr)
                {
                    auto* src = range->source;
                    Private::UnlinkLayer(src->book, range->lowerLink);
                    src->ops->UnrefObj(src);
                }

                PoolFreeWired(range, sizeof(*range), SpaceHeapTag);
            }

            HwMapDestroy(newSpace->map);
            PoolFreeWired(newSpace, sizeof(VmSpace), SpaceHeapTag);

            //a turned-away fault waits on `rangesMutex`, so before it goes
            source.faultGate.Store(false, sl::Release);
            ReleaseSxMutexExclusive(&source.rangesMutex);
            ReleaseMutex(&source.freeRangesMutex);

            return cloneStatus;
        }

        *clone = newSpace;
        source.faultGate.Store(false, sl::Release);
        ReleaseSxMutexExclusive(&source.rangesMutex);
        ReleaseMutex(&source.freeRangesMutex);

        return NpkStatus::Success;
    }
}
