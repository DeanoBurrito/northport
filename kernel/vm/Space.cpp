#include <private/Vm.hpp>
#include <lib/Maths.hpp>
#include <lib/Units.hpp>

namespace Npk
{
    constexpr HeapTag SpaceHeapTag = NPK_MAKE_HEAP_TAG("Spac");

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
        Log("Pool space: 0x%tx-0x%tx (%zu.%zu %sB)",
            LogLevel::Verbose, poolBase, poolBase + poolSize,
            conv.major, conv.minor, conv.prefix);

        //2. TODO: implement csegs and explain why this is useful/how it works.
        const uintptr_t cacheBase = poolBase + poolSize;
        const size_t cacheSize = AlignDownPage(lowLen / 4);

        conv = sl::ConvertUnits(cacheSize);
        Log("Cache space: 0x%tx-0x%tx (%zu.%zu %sB)",
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

        ResetMutex(&mySpace->freeRangesMutex, 1);
        ResetSxMutex(&mySpace->rangesMutex, 1);

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
        Log("General space (low): 0x%tx-0x%tx (%zu.%zu %sB)",
            LogLevel::Verbose, systemLowBase, systemLowBase + systemLowSize,
            conv.major, conv.minor, conv.prefix);

        conv = sl::ConvertUnits(highLen);
        Log("General space (high): 0x%tx-0x%tx (%zu.%zu %sB)",
            LogLevel::Verbose, highBase, highBase + highLen,
            conv.major, conv.minor, conv.prefix);
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
            tree.Insert(range);

            return;
        }
        else if (base + len == range->base + range->length)
        {
            range->length -= len;
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
        latest->largestChild = range->length;
        tree.Insert(range);
    }

    NpkStatus SpaceAlloc(VmSpace& space, uintptr_t* addr, size_t length,
        AllocConstraints constr)
    {
        //TODO: support alignment requests
        NPK_CHECK(constr.alignment == 0, NpkStatus::InvalidArg);

        length = AlignUpPage(length);
        constr.minAddr = AlignUpPage(constr.minAddr);
        constr.maxAddr = AlignDownPage(constr.maxAddr);
        if ((constr.preferredAddr & PageMask()) != 0)
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

        if (!AcquireMutex(&space.freeRangesMutex, constr.timeout, 
            NPK_WAIT_LOCATION))
        {
            PoolFreeWired(spareRange, sizeof(*spareRange), SpaceHeapTag);
            return NpkStatus::InternalError;
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
                    PoolFreeWired(spareRange, sizeof(*spareRange), SpaceHeapTag);

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

        auto scan = space.freeRanges.GetRoot();
        while (scan != nullptr)
        {
            //TODO: minAddr and maxAddr
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
                break;

            if (last != nullptr && last->largestChild >= length)
            {
                scan = last;
                continue;
            }

            //no nodes large enough, gtfo
            scan = nullptr;
            break;
        }

        NpkStatus status = NpkStatus::Shortage;
        if (scan != nullptr)
        {
            status = NpkStatus::Success;
            *addr = scan->base;
            BreakdownRange(space.freeRanges, scan, scan->base, length, 
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

        if (!AcquireMutex(&space.freeRangesMutex, timeout, NPK_WAIT_LOCATION))
            return NpkStatus::InternalError;

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

        //try coalesce with nearby free ranges, if any
        VmFreeRange* latest = nullptr;
        if (succ != nullptr && succ->base == base + length)
        {
            space.freeRanges.Remove(succ);
            length += succ->length;

            latest = succ;
            succ = nullptr;
        }
        if (pred != nullptr && pred->base + pred->length == base)
        {
            space.freeRanges.Remove(pred);
            length += pred->length;
            base = pred->base;

            if (latest != nullptr)
            {
                latest = pred;
                pred = nullptr;
            }
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

        if (pred != nullptr)
            PoolFreeWired(pred, sizeof(*pred), SpaceHeapTag);
        if (succ != nullptr)
            PoolFreeWired(succ, sizeof(*succ), SpaceHeapTag);
        if (spareRange != nullptr)
            PoolFreeWired(spareRange, sizeof(*spareRange), SpaceHeapTag);

        return NpkStatus::Success;
    }

    //NOTE: assumes space.rangesMutex is held (shared or exclusive)
    static NpkStatus SpaceLookupLocked(VmRange** found, VmSpace& space, 
        uintptr_t addr, size_t length)
    {
        const uintptr_t end = AlignUpPage(addr + length);
        addr = AlignDownPage(addr);

        auto scan = space.ranges.GetRoot();
        while (scan != nullptr)
        {
            const uintptr_t scanEnd = scan->base + scan->length;
            if (addr < scanEnd && scan->base < end)
            {
                if (found != nullptr)
                    *found = scan;
                return NpkStatus::Success;
            }

            if (addr < scan->base)
                scan = space.ranges.GetLeft(scan);
            else if (addr > scan->base)
                scan = space.ranges.GetRight(scan);
            else
            {
                if (found != nullptr)
                    *found = scan;
                return NpkStatus::Success;
            }
        }

        return NpkStatus::BadVaddr;
    }

    NpkStatus SpaceAttach(VmRange** range, VmSpace& space, uintptr_t base, 
        size_t length, VmSource* source, size_t srcOffset, VmFlags flags)
    {
        length = AlignUpPage(length);

        if (flags.Has(VmFlag::AmapNeedsCopy))
            return NpkStatus::InvalidArg;

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

        if (!AcquireSxMutexExclusive(&space.rangesMutex, sl::NoTimeout,
            NPK_WAIT_LOCATION))
        {
            if (allocatedVaddrs)
                SpaceFree(space, base, length);
            return NpkStatus::InternalError;
        }

        auto result = SpaceLookupLocked(nullptr, space, base, length);
        if (result == NpkStatus::Success)
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
        vmr->flags = flags;
        vmr->base = base;
        vmr->length = length;
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

        const uintptr_t base = range->base;
        const size_t length = range->length;

        if (!AcquireSxMutexExclusive(&space.rangesMutex, sl::NoTimeout,
            NPK_WAIT_LOCATION))
            return NpkStatus::InternalError;

        VmRange* check = nullptr;
        auto result = SpaceLookupLocked(&check, space, range->base, 1);
        if (result != NpkStatus::Success || range == check)
        {
            ReleaseSxMutexExclusive(&space.rangesMutex);
            return NpkStatus::InvalidArg;
        }
        (void)check;

        space.ranges.Remove(range);

        if (range->amapRef.Valid())
            range->amapRef.Release();

        if (range->source != nullptr)
        {
            range->source->ops->UnrefObj(range->source);
            range->source = nullptr;
        }

        PoolFreeWired(range, sizeof(*range), SpaceHeapTag);
        range = nullptr;
        (void)range;

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

    NpkStatus SpaceClone(VmSpace** clone, VmSpace& source)
    {
        if (!AcquireMutex(&source.freeRangesMutex, sl::NoTimeout,
            NPK_WAIT_LOCATION))
            return NpkStatus::InternalError;

        if (!AcquireSxMutexExclusive(&source.rangesMutex, sl::NoTimeout,
            NPK_WAIT_LOCATION))
        {
            ReleaseMutex(&source.freeRangesMutex);
            return NpkStatus::InternalError;
        }

        void* ptr = PoolAllocWired(sizeof(VmSpace), SpaceHeapTag);
        if (ptr == nullptr)
        {
            ReleaseSxMutexExclusive(&source.rangesMutex);
            ReleaseMutex(&source.freeRangesMutex);

            return NpkStatus::Shortage;
        }

        auto* newSpace = new(ptr) VmSpace {};
        NPK_ASSERT(ResetMutex(&newSpace->freeRangesMutex, 1));
        NPK_ASSERT(ResetSxMutex(&newSpace->rangesMutex, 1));

        bool carryOn = true;
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
            ptr = PoolAllocWired(sizeof(VmRange), SpaceHeapTag);
            if (ptr == nullptr)
            {
                carryOn = false;
                break;
            }

            //TODO: mark amap as copy-on-write, means modifying any existing
            //mappings to the amap.

            auto* latest = new(ptr) VmRange {};
            latest->base = it->base;
            latest->length = it->length;
            latest->flags = it->flags;
            latest->offset = it->offset;
            latest->amapOffset = it->amapOffset;
            latest->amapRef = it->amapRef;
            latest->source = it->source;

            if (latest->source != nullptr)
            {
                auto src = latest->source;
                PagerFlags pagerFlags {};
                if (it->flags.Has(VmFlag::Write))
                    pagerFlags.Set(PagerFlag::Write);

                NPK_ASSERT(src->ops->RefObj(src, pagerFlags));
            }

            newSpace->ranges.Insert(latest);

            if (!it->source->ops->RefObj(it->source, {}))
            {
                carryOn = false;
                break;
            }
            else
                latest->source = it->source;
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
                PoolFreeWired(range, sizeof(*range), SpaceHeapTag);
            }

            PoolFreeWired(newSpace, sizeof(VmSpace), SpaceHeapTag);

            ReleaseSxMutexExclusive(&source.rangesMutex);
            ReleaseMutex(&source.freeRangesMutex);

            return NpkStatus::Shortage;
        }

        *clone = newSpace;
        ReleaseSxMutexExclusive(&source.rangesMutex);
        ReleaseMutex(&source.freeRangesMutex);

        return NpkStatus::Success;
    }

    NpkStatus SpaceLookup(VmRange** found, VmSpace& space, uintptr_t addr)
    {
        NPK_CHECK(found != nullptr, NpkStatus::InvalidArg);

        if (!AcquireSxMutexShared(&space.rangesMutex, {}, NPK_WAIT_LOCATION))
            return NpkStatus::InternalError;

        auto status = SpaceLookupLocked(found, space, addr, 1);
        ReleaseSxMutexShared(&space.rangesMutex);

        return status;
    }
}
