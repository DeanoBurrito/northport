#include <VmPrivate.hpp>
#include <Maths.hpp>
#include <UnitConverter.hpp>

namespace Npk
{
    constexpr HeapTag SpaceHeapTag = NPK_MAKE_HEAP_TAG("Spac");

    void InitKernelVmSpace(uintptr_t lowBase, size_t lowLen, uintptr_t highBase,
        size_t highLen)
    {
        /* The kernel address space operates as a few distinct parts:
         * - pool space: provides the general purpose (read: malloc) allocators.
         * - cache space: used to map and access views of cached files.
         * - system space: general purpose address space, for loading drivers,
         *   accessing pinned (userspace/dma) buffers, other uses.
         * `AllocateInSpace()` will only return addresses from system space,
         * (there are other APIs to get addresses in the other spaces), but
         * all spaces uses the same `VmSpace`, returned from `KernelSpace()`.
         *
         * The size of the pool and cache space zones is calculated here and
         * fixed, system space is whatever is left over.
         */

        NPK_ASSERT((lowBase & PageMask()) == 0);
        NPK_ASSERT((highBase & PageMask()) == 0);

        const uintptr_t poolBase = lowBase;
        const size_t poolSize = AlignDownPage(lowLen / 4);
        Private::InitPool(poolBase, poolSize);

        auto conv = sl::ConvertUnits(poolSize);
        Log("Pool space: 0x%tx-0x%tx (%zu.%zu %sB)",
            LogLevel::Verbose, poolBase, poolBase + poolSize,
            conv.major, conv.minor, conv.prefix);

        const uintptr_t cacheBase = poolBase + poolSize;
        const size_t cacheSize = AlignDownPage(lowLen / 4);

        conv = sl::ConvertUnits(cacheSize);
        Log("Cache space: 0x%tx-0x%tx (%zu.%zu %sB)",
            LogLevel::Verbose, cacheBase, cacheBase + cacheSize,
            conv.major, conv.minor, conv.prefix);

        const uintptr_t systemLowBase = cacheBase + cacheSize;
        const size_t systemLowSize = AlignDownPage(lowLen / 2);

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
        tree.Remove(range);
        base = sl::Max(range->base, base);

        if (base == range->base)
        {
            range->base += len;
            range->length -= len;
            return;
        }
        else if (base + len == range->base + range->length)
        {
            range->length -= len;
            return;
        }

        auto latest = *spare;
        *spare = nullptr;

        latest->base = range->base;
        latest->length = base - latest->base;
        latest->largestChild = latest->length;
        tree.Insert(latest);

        range->length += range->base;
        range->base = base + len;
        range->length -= range->base;
        latest->largestChild = range->length;
        tree.Insert(range);
    }

    VmStatus SpaceAlloc(VmSpace& space, uintptr_t* addr, size_t length,
        AllocConstraints constr)
    {
        //TODO: support alignment requests
        NPK_CHECK(constr.alignment == 0, VmStatus::InvalidArg);

        length = AlignUpPage(length);
        constr.minAddr = AlignUpPage(constr.minAddr);
        constr.maxAddr = AlignDownPage(constr.maxAddr);
        if ((constr.preferredAddr & PageMask()) != 0)
            return VmStatus::InvalidArg;

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
            return VmStatus::Shortage;
        VmFreeRange* spareRange = new(sparePtr) VmFreeRange{};

        if (!AcquireMutex(&space.freeRangesMutex, constr.timeout, 
            NPK_WAIT_LOCATION))
        {
            PoolFreeWired(spareRange, sizeof(*spareRange), SpaceHeapTag);
            return VmStatus::InternalError;
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

                //preferredAddr is within the current node, ensure it fits
                if (constr.preferredAddr + length >= scan->base + scan->length)
                    scan = nullptr;
                break;
            }

            if (scan != nullptr)
            {
                //preferred address is available.
                BreakdownRange(space.freeRanges, scan, constr.preferredAddr, 
                    length, &spareRange);
                ReleaseMutex(&space.freeRangesMutex);

                *addr = constr.preferredAddr;
                if (spareRange != nullptr)
                    PoolFreeWired(spareRange, sizeof(*spareRange), SpaceHeapTag);

                return VmStatus::Success;
            }
            else if (constr.hardPreference)
            {
                //failed to allocate and its a hard requirement
                ReleaseMutex(&space.freeRangesMutex);
                PoolFreeWired(spareRange, sizeof(*spareRange), SpaceHeapTag);

                return VmStatus::InUse;
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

        VmStatus status = VmStatus::Shortage;
        if (scan != nullptr)
        {
            status = VmStatus::Success;
            *addr = scan->base;
            BreakdownRange(space.freeRanges, scan, scan->base, length, 
                &spareRange);
        }

        ReleaseMutex(&space.freeRangesMutex);
        if (spareRange != nullptr)
            PoolFreeWired(spareRange, sizeof(*spareRange), SpaceHeapTag);

        return status;
    }

    VmStatus SpaceFree(VmSpace& space, uintptr_t base, size_t length, 
        sl::TimeCount timeout)
    {
        void* sparePtr = PoolAllocWired(sizeof(VmFreeRange), SpaceHeapTag);
        if (sparePtr == nullptr)
            return VmStatus::Shortage;
        VmFreeRange* spareRange = new(sparePtr) VmFreeRange{};

        if (!AcquireMutex(&space.freeRangesMutex, timeout, NPK_WAIT_LOCATION))
            return VmStatus::InternalError;

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

        return VmStatus::Success;
    }

    VmStatus LookupRangeInSpace(VmRange** found, VmSpace& space, uintptr_t addr)
    {
        NPK_CHECK(found != nullptr, VmStatus::InvalidArg);

        if (!AcquireSxMutexShared(&space.rangesMutex, {}, NPK_WAIT_LOCATION))
            return VmStatus::InternalError;

        auto scan = space.ranges.GetRoot();
        while (scan != nullptr)
        {
            if (addr < scan->base)
            {
                scan = space.ranges.GetLeft(scan);
                continue;
            }

            if (addr >= scan->base + scan->length)
            {
                scan = space.ranges.GetRight(scan);
                continue;
            }

            *found = scan;
            break;
        }

        ReleaseSxMutexShared(&space.rangesMutex);

        if (*found != nullptr)
            return VmStatus::Success;
        else
            return VmStatus::BadVaddr;
    }
}
