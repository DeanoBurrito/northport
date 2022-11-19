#include <memory/Vmm.h>
#include <memory/virtual/VmDriver.h>
#include <memory/Heap.h>
#include <arch/Platform.h>
#include <arch/Paging.h>
#include <boot/LinkerSyms.h>
#include <debug/Log.h>
#include <Memory.h>
#include <Maths.h>
#include <NativePtr.h>
#include <Lazy.h>

namespace Npk::Memory
{
    sl::Lazy<VMM> kernelVmm;
    void VMM::InitKernel()
    {
        PagingSetup(); //arch-specific paging setup (enable NX, etc)
        Virtual::VmDriver::InitEarly(); //bring up basic VM drivers, including kernel driver.

        kernelVmm.Init(VmmKey{});
        Heap::Global().Init();
    }

    VMM& VMM::Kernel()
    { return *kernelVmm; }

    VMM& VMM::Current()
    { return *static_cast<VMM*>(CoreLocal().vmm); }

    VMM::VirtualMemoryManager()
    {
        sl::ScopedLock scopeLock(rangesLock);

        globalLowerBound = PageSize; //dont allocate in page 0.
        globalUpperBound = ~hhdmBase;
        ptRoot = InitPageTables(&localKernelGen);
        
        Log("User VMM initialized.", LogLevel::Info);
    }

    VMM::VirtualMemoryManager(VmmKey)
    {
        sl::ScopedLock scopeLock(rangesLock);

        //protect hhdm from allocations, we allocate `hhdm_limit` bytes for the heap too.
        globalLowerBound = hhdmBase + hhdmLength + GetHhdmLimit();
        globalUpperBound = sl::AlignDown((uintptr_t)KERNEL_BLOB_BEGIN, GiB);
        ptRoot = kernelMasterTables;

        LoadTables(ptRoot);
        Log("Kernel VMM initiailized: ptRoot=0x%lx, allocSpace=0x%lx - 0x%lx", LogLevel::Info, 
            (uintptr_t)ptRoot, globalLowerBound, globalUpperBound);
    }

    VMM::~VirtualMemoryManager()
    {} //TODO: VMM teardown

    void VMM::MakeActive()
    {
        //ensure this VMM's kernel mappings match the master set
        if (__atomic_load_n(&kernelTablesGen, __ATOMIC_RELAXED) != localKernelGen)
        {
            localKernelGen = kernelTablesGen;
            SyncKernelTables(ptRoot);
        }

        LoadTables(ptRoot);
    }

    void VMM::HandleFault(uintptr_t addr, VmFaultFlags flags)
    {
        if (addr < globalLowerBound || addr >= globalUpperBound)
            Log("Bad page fault at 0x%lx, flags=0x%lx", LogLevel::Fatal, addr, (size_t)flags);
        
        rangesLock.Lock();
        VmRange* range = nullptr;
        for (auto it = ranges.Begin(); it != ranges.End(); ++it)
        {
            if (addr < it->base)
                break;
            if (addr > it->Top())
                continue;
            
            if (addr >= it->base && addr < it->Top())
                range = &(*it);
        }
        
        if (range == nullptr)
            Log("Bad page fault at 0x%lx, flags=0x%lx", LogLevel::Fatal, addr, (size_t)flags);
        rangesLock.Unlock();

        using namespace Virtual;
        VmDriver* driver = VmDriver::GetDriver((VmDriverType)((size_t)range->flags >> 48));
        ASSERT(driver != nullptr, "Active range with no driver");

        VmDriverContext context{ ptRoot, ptLock, *range };
        EventResult result = driver->HandleEvent(context, EventType::PageFault, addr, (uintptr_t)flags);
        
        ASSERT(result == EventResult::Continue, "TODO: handle other page-fault results");
        Log("Good page fault: 0x%lx, ec=0x%lx", LogLevel::Debug, addr, (size_t)flags);
    }

    sl::Opt<VmRange> VMM::Alloc(size_t length, uintptr_t initArg, VmFlags flags, uintptr_t lowerBound, uintptr_t upperBound)
    {
        using namespace Virtual;

        //check we have a driver for this allocation type
        //the top 16 bits of the flags contain the allocaction type (anon, kernel-special, shared, etc...)
        VmDriver* driver = VmDriver::GetDriver((VmDriverType)((size_t)flags >> 48));
        if (driver == nullptr)
        {
            Log("VMM failed to allocate 0x%lu bytes, no driver (requested type %lu).", LogLevel::Error, length, (size_t)flags >> 48);
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
        rangesLock.Unlock();
        return {};
        
    alloc_range_claim:
        VmRange range;
        if (insertBefore == ranges.End())
            range = ranges.EmplaceBack( allocAt, length, flags, 0ul );
        else
            range = *ranges.Insert(insertBefore, { allocAt, length, flags, 0ul });
        rangesLock.Unlock();

        VmDriverContext context{ ptRoot, ptLock, range };
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
        const uint16_t driverIndex = (size_t)range.flags >> 48;
        VmDriver* driver = VmDriver::GetDriver((VmDriverType)driverIndex);
        ASSERT(driver != nullptr, "Attempted to free range with no associated driver");
        
        constexpr size_t DetachTryCount = 3;
        VmDriverContext context { ptRoot, ptLock, range };
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

    size_t VMM::CopyIn(void* foreignBase, void* localBase, size_t length)
    {
        if (!RangeExists((uintptr_t)foreignBase, length, {}))
            return 0;

        sl::NativePtr local = localBase;
        size_t count = 0;

        while (count < length)
        {
            auto maybePhys = GetPhysicalAddr(ptRoot, (uintptr_t)foreignBase + count);
            if (!maybePhys.HasValue())
                HandleFault((uintptr_t)foreignBase + count, VmFaultFlags::None);
            maybePhys = GetPhysicalAddr(ptRoot, (uintptr_t)foreignBase + count);

            const size_t copyLength = sl::Min(PageSize, length - count);
            sl::memcopy(local.ptr, reinterpret_cast<void*>(AddHhdm(*maybePhys)), copyLength);
            count += copyLength;
            local.raw += copyLength;
        }
        return count;
    }

    size_t VMM::CopyOut(void* localBase, void* foreignBase, size_t length)
    {
        sl::NativePtr local = localBase;
        size_t count = 0;

        while (count < length)
        {
            auto maybePhys = GetPhysicalAddr(ptRoot, (uintptr_t)foreignBase + count);
            if (!maybePhys)
                return count;
            
            const size_t copyLength = sl::Min(PageSize, length - count);
            sl::memcopy(AddHhdm(reinterpret_cast<void*>(*maybePhys)), local.ptr, copyLength);
            local.raw += copyLength;
        }
        return count;
    }
}
