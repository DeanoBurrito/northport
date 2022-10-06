#include <memory/Vmm.h>
#include <memory/virtual/VmDriver.h>
#include <memory/Heap.h>
#include <arch/Paging.h>
#include <arch/Platform.h>
#include <boot/LinkerSyms.h>
#include <debug/Log.h>

namespace Npk::Memory
{
    constexpr PageFlags ConvertFlags(VmFlags flags)
    {
        /*  VMFlags are stable across platforms, while PageFlags have different meanings
            depending on the ISA. This provides source-level translation between the two. */
        PageFlags value = PageFlags::None;
        if (flags & VmFlags::Write)
            value |= PageFlags::Write;
        if (flags & VmFlags::Execute)
            value |= PageFlags::Execute;
        if (flags & VmFlags::User)
            value |= PageFlags::User;
        
        return value;
    }

    alignas(VMM) uint8_t kernelVmm[sizeof(VMM)];
    void VMM::InitKernel()
    {
        //arch-specific paging setup (enable NX, etc)
        PagingSetup();
        //bring up basic VM drivers, including kernel driver.
        Virtual::VmDriver::InitEarly();

        new (kernelVmm) VMM(VmmKey{});
        Heap::Global().Init();
    }

    VMM& VMM::Kernel()
    { return *reinterpret_cast<VMM*>(kernelVmm); }

    VMM& VMM::Current()
    { return *static_cast<VMM*>(CoreLocal().vmm); }

    VMM::VirtualMemoryManager()
    {
        globalLowerBound = PageSize; //dont allocate in page 0.
        globalUpperBound = ~hhdmBase;
        Log("User VMM initialized.", LogLevel::Info);
    }

    VMM::VirtualMemoryManager(VmmKey)
    {
        sl::ScopedLock scopeLock(rangesLock);

        //protect the hhdm, heap and kernel binary from allocations.
        //TODO: change the heap to use the anon vm driver.
        globalLowerBound = hhdmBase + HhdmLimit + HeapLimit;
        globalUpperBound = sl::AlignDown((uintptr_t)KERNEL_BLOB_BEGIN, GiB);
        ptRoot = kernelMasterTables;

        LoadTables(ptRoot);
        Log("Kernel VMM initiailized: ptRoot=0x%lx, allocSpace=0x%lx - 0x%lx", LogLevel::Info, 
            (uintptr_t)ptRoot, globalLowerBound, globalUpperBound);
    }

    VMM::~VirtualMemoryManager()
    {} //TODO: VMM teardown

    sl::Opt<VmRange> VMM::Alloc(size_t length, uintptr_t initArg, VmFlags flags, uintptr_t lowerBound, uintptr_t upperBound)
    {
        using namespace Virtual;

        //check we have a driver for this allocation type
        //the top 16 bits of the flags contain the allocaction type (anon, kernel-special, shared, etc...)
        VmDriver* driver = VmDriver::GetDriver((VmDriverType)(flags >> 48));
        if (driver == nullptr)
        {
            Log("VMM failed to allocate 0x%lu bytes, no driver (requested type %lu).", LogLevel::Error, length, flags >> 48);
            return {};
        }

        //make sure things are valid
        length = sl::AlignUp(length, PageSize);
        lowerBound = sl::Max(globalLowerBound, sl::AlignUp(lowerBound, PageSize));
        upperBound = sl::Min(globalUpperBound, sl::AlignDown(upperBound, PageSize));
        if (lowerBound + length >= upperBound)
            return {};
        
        rangesLock.Lock();
        auto insertBefore = ranges.Begin();
        uintptr_t allocAt = lowerBound;

        //find a virtual range big enough
        //first try: no existing ranges, or there's space before the first range
        if (ranges.Size() == 0 || lowerBound + length < insertBefore->base)
        {
            if (lowerBound + length < upperBound)
                goto alloc_range_claim;
        }
        
        //second try: search the gaps between adjacent ranges.
        //the list of ranges is always sorted, so this is easy.
        while (insertBefore != ranges.End())
        {
            auto lowIt = insertBefore;
            ++insertBefore;
            if (insertBefore == ranges.End())
                break;

            if (insertBefore->base - lowIt->Top() < length)
                continue;
            
            allocAt = lowIt->Top();
            goto alloc_range_claim;
        }
        
        //third try: check for enough after the last range.
        if (sl::Max(ranges.Back().Top(), lowerBound) + length < upperBound)
        {
            allocAt = sl::Max(ranges.Back().Top(), lowerBound);
            goto alloc_range_claim;
        }
        
        //couldn't find space anywhere
        return {};
        
    alloc_range_claim:
        VmRange range;
        if (insertBefore == ranges.End())
            range = ranges.EmplaceBack( allocAt, length, flags, 0ul );
        else
            range = *ranges.Insert(insertBefore, { allocAt, length, flags, 0ul });
        rangesLock.Unlock();

        VmDriverContext context{ ptRoot, ptLock, range.base, length, 0ul, flags };
        auto maybeToken = driver->AttachRange(context, initArg);
        if (!maybeToken)
        {
            //driver was unable to satisfy our request
            ASSERT_UNREACHABLE(); //yeah, error handled.
        }
        
        range.token = *maybeToken;
        return range;
    }

    bool VMM::Free(uintptr_t base, size_t length)
    {
        rangesLock.Lock();

        //find the range, remove it from list
        VmRange range {};
        for (auto it = ranges.Begin(); it != ranges.End(); ++it)
        {
            if (it->base == base && it->length == length)
            {
                range = sl::Move(*it);
                ranges.Erase(it);
                break;
            }
        }
        rangesLock.Unlock();

        //detach range from driver
        using namespace Virtual;
        const uint16_t driverIndex = range.flags >> 48;
        VmDriver* driver = VmDriver::GetDriver((VmDriverType)driverIndex);
        ASSERT(driver != nullptr, "Attempted to free range with no associated driver");
        
        constexpr size_t DetachTryCount = 3;
        VmDriverContext context { ptRoot, ptLock, range.base, range.length, range.token, range.flags };
        bool success = driver->DetachRange(context);
        for (size_t i = 0; i < DetachTryCount && !success; i++)
        {
            Log("Attempt %lu: VMDriver (index %u, %s) failed to detach from range at 0x%lx.", LogLevel::Warning, 
                i + 1ul, driverIndex, VmDriverTypeStrs[driverIndex], i);
            success = driver->DetachRange(context);
        }
        if (!success)
        {
            Log("VM driver (index %u, %s) failed %lu to detach from VM range at 0x%lx.", LogLevel::Error, 
                driverIndex, VmDriverTypeStrs[driverIndex], DetachTryCount, range.base);
            //TODO: if we failed to remove it, it should probably go back in the list to block
            //further allocations in that particular address space.
        }

        return true;
    }

    bool VMM::RangeExists(uintptr_t base, size_t length, sl::Opt<VmFlags> flags)
    {
        for (auto it = ranges.Begin(); it != ranges.End(); ++it)
        {
            if (it->base > base + length)
                return false;
            if (base >= it->base && base + length <= it->Top())
                return flags ? (it->flags & *flags) == *flags : true;
        }

        return false;
    }
}
