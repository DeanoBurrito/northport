#include <memory/Vmm.h>
#include <memory/virtual/VmDriver.h>
#include <memory/Pmm.h>
#include <memory/Heap.h>
#include <arch/Hat.h>
#include <boot/LinkerSyms.h>
#include <debug/Log.h>
#include <Bitmap.h>
#include <Memory.h>
#include <Lazy.h>

namespace Npk::Memory
{
    VmRange* VMM::AllocStruct()
    {
        sl::ScopedLock scopeLock(allocLock);
        
        VmSlabAlloc* allocator = rangesAlloc;
        while (allocator != nullptr)
        {
            for (size_t i = 0; i < VmSlabCount; i++)
            {
                if (sl::BitmapGet(allocator->bitmap, i))
                    continue;
                
                sl::BitmapSet(allocator->bitmap, i);
                return new (&allocator->slabs[i]) VmRange{};
            }

            if (allocator->next == nullptr)
                break;
            allocator = allocator->next;
        }

        VmSlabAlloc* latestSlab = reinterpret_cast<VmSlabAlloc*>(PMM::Global().Alloc() + hhdmBase);
        sl::memset(latestSlab->bitmap, 0, VmBitmapBytes);
        sl::BitmapSet(latestSlab->bitmap, 0);
        if (allocator == nullptr)
            rangesAlloc = latestSlab;
        else
            allocator->next = latestSlab;

        return new (&latestSlab->slabs[0]) VmRange{};
    }

    void VMM::FreeStruct(VmRange* item)
    {
        sl::ScopedLock scopeLock(allocLock);
        
        const uintptr_t allocAddr = sl::AlignDown((uintptr_t)item, PageSize);
        VmSlabAlloc* allocator = reinterpret_cast<VmSlabAlloc*>(allocAddr);

        const size_t index = ((uintptr_t)item - allocAddr) / sizeof(VmRange);
        sl::BitmapClear(allocator->bitmap, index);
    }
    
    sl::Lazy<VMM> kernelVmm;
    void VMM::InitKernel()
    {
        HatInit(); //arch-specific setup of the MMU.
        Virtual::VmDriver::InitEarly(); //bring up basic VM drivers, including kernel driver.
        kernelVmm.Init(VmmKey{}); //VmmKey calls the constructor for the kernel vmm.
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
        hatMap = InitNewMap();

        Log("User VMM initialized.", LogLevel::Info);
    }

    VMM::VirtualMemoryManager(VmmKey)
    {
        sl::ScopedLock scopeLock(rangesLock);

        //protect hhdm from allocations, we also reserve a region right after hhdm for early slab allocation.
        globalLowerBound = hhdmBase + hhdmLength;
        globalUpperBound = sl::AlignDown((uintptr_t)KERNEL_BLOB_BEGIN, GiB);

        hatMap = KernelMap();
        MakeActiveMap(hatMap);
        Log("Kernel VMM initialized: bounds=0x%lx - 0x%lx", LogLevel::Info, globalLowerBound, globalUpperBound);
    }

    VMM::~VirtualMemoryManager()
    {} //TODO: VMM teardown

    void VMM::MakeActive()
    {
        if (hatMap != KernelMap())
            SyncWithMasterMap(hatMap);
        MakeActiveMap(hatMap);
    }

    void VMM::HandleFault(uintptr_t addr, VmFaultFlags flags)
    {
        if (addr < globalLowerBound || addr >= globalUpperBound)
            Log("Bad page fault at 0x%lx, flags=0x%lx", LogLevel::Fatal, addr, (size_t)flags);
        
        rangesLock.Lock();
        VmRange* range = nullptr;
        for (auto it = ranges.Begin(); it != ranges.End(); ++it)
        {
            if (addr < it->base) //TODO: use an acceleration structure for lookups here
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

        VmDriverContext context { mapLock, hatMap, *range };
        EventResult result = driver->HandleEvent(context, EventType::PageFault, addr, (uintptr_t)flags);
        
        if (result != EventResult::Continue)
            Log("Bad page fault at 0x%lx, flags=0x%lx, result=%lu", LogLevel::Fatal, addr, (size_t)flags, (size_t)result);
        Log("Good page fault: 0x%lx, ec=0x%lx", LogLevel::Debug, addr, (size_t)flags);
    }

    void VMM::PrintRanges(void (*PrintFunc)(const char*, ...))
    {
        ASSERT(PrintFunc != nullptr, "Tried to print VMM ranges with null function pointer");

        constexpr char VmFlagChars[] = { 'w', 'x', 'u' };
        constexpr const char* VmTypeStrs[] = { "Unknown", "Anon", "Mmio" };

        constexpr size_t FlagsCount = 3;
        char flagsBuff[FlagsCount + 1];
        
        sl::ScopedLock scopeLock(rangesLock);
        for (auto it = ranges.Begin(); it != ranges.End(); ++it)
        {
            for (size_t i = 0; i < FlagsCount; i++)
                flagsBuff[i] = ((size_t)it->flags & (1 << i)) ? VmFlagChars[i] : '-';
            flagsBuff[FlagsCount] = 0;
            
            const size_t type = (size_t)it->flags >> 48;
            PrintFunc("0x%016lx 0x%016lx 0x%06lx %s %s\r\n", it->base, it->Top(), it->length,
                flagsBuff, VmTypeStrs[type]);
        }
    }

    sl::Opt<VmRange> VMM::Alloc(size_t length, uintptr_t initArg, VmFlags flags, uintptr_t lowerBound, uintptr_t upperBound)
    {
        using namespace Virtual;

        //check we have a driver for this allocation type
        //the top 16 bits of the flags contain the allocaction type (anon, kernel-special, shared, etc...)
        VmDriver* driver = VmDriver::GetDriver((VmDriverType)((size_t)flags >> 48));
        VALIDATE(driver != nullptr, {}, "VMM Alloc failed, invalid memory driver type.");

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
        VmRange* range = AllocStruct();
        range->base = allocAt;
        range->length = length;
        range->flags = flags;
        if (insertBefore == ranges.End())
            ranges.PushBack(range);
        else
            ranges.Insert(insertBefore, range);
        rangesLock.Unlock();

        VmDriverContext context { mapLock, hatMap, *range };
        auto maybeToken = driver->AttachRange(context, initArg);
        if (!maybeToken)
        {
            //driver was unable to satisfy our request
            ASSERT_UNREACHABLE(); //yeah, error handled.
        }
        
        range->token = *maybeToken;
        return *range;
    }

    bool VMM::Free(uintptr_t base)
    {
        rangesLock.Lock();

        //find the range, remove it from list
        VmRange* range = nullptr;
        for (auto it = ranges.Begin(); it != ranges.End(); ++it)
        {
            if (base < it->base || base >= it->Top())
                continue;
            
            range = static_cast<VmRange*>(ranges.Erase(it.entry));
            break;
        }
        rangesLock.Unlock();
        
        VALIDATE(range != nullptr, false, "Couldn't free VM Range: it does not exist.");

        //detach range from driver
        using namespace Virtual;
        const uint16_t driverIndex = (size_t)range->flags >> 48;
        VmDriver* driver = VmDriver::GetDriver((VmDriverType)driverIndex);
        ASSERT(driver != nullptr, "Attempted to free range with no associated driver");
        
        constexpr size_t DetachTryCount = 3;
        VmDriverContext context { mapLock, hatMap, *range };
        bool success = driver->DetachRange(context);
        for (size_t i = 0; i < DetachTryCount && !success; i++)
        {
            Log("Attempt %lu: VMDriver (index %u, %s) failed to detach from range at 0x%lx.", LogLevel::Warning, 
                i + 1ul, driverIndex, VmDriverTypeStrs[driverIndex], i);
            success = driver->DetachRange(context);
        }

        ASSERT(success, "Failed to detach VM range");
        //TODO: we should handle this more gracefully in the future, when we have ways this can fail.
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

    sl::Opt<uintptr_t> VMM::GetPhysical(uintptr_t vaddr)
    {
        return GetMap(hatMap, vaddr);
    }

    size_t VMM::CopyIn(void* foreignBase, void* localBase, size_t length)
    {
        if (!RangeExists((uintptr_t)foreignBase, length, {}))
            return 0;

        sl::NativePtr local = localBase;
        size_t count = 0;

        while (count < length)
        {
            auto maybePhys = GetMap(hatMap, (uintptr_t)foreignBase + count);
            if (!maybePhys.HasValue())
            {
                HandleFault((uintptr_t)foreignBase + count, VmFaultFlags::Write);
                maybePhys = GetMap(hatMap, (uintptr_t)foreignBase + count);
            }
            
            size_t copyLength = sl::Min(PageSize, length - count);
            //first copy can be misaligned (in the destination address space), so handle that.
            if (count == 0)
                copyLength = sl::AlignUp((uintptr_t)foreignBase, PageSize) - (uintptr_t)foreignBase;

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
            auto maybePhys = GetMap(hatMap, (uintptr_t)foreignBase + count);
            if (!maybePhys)
                return count;
            
            size_t copyLength = sl::Min(PageSize, length - count);
            if (count == 0)
                copyLength = sl::AlignUp((uintptr_t)foreignBase, PageSize) - (uintptr_t)foreignBase;
            sl::memcopy(AddHhdm(reinterpret_cast<void*>(*maybePhys)), local.ptr, copyLength);
            local.raw += copyLength;
        }
        return count;
    }
}
