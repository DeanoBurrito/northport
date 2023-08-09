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
#include <UnitConverter.h>

namespace Npk::Memory
{
    constexpr const size_t VmmSlabSizes[(size_t)VmmMetaType::Count] = 
    { 
        sizeof(VmRange), 
        sizeof(VmHole)
    };

    constexpr size_t VmmMetaSlabPages = 1;

    VmmMetaSlab* VMM::CreateMetaSlab(VmmMetaType type)
    {
        const size_t index = static_cast<size_t>(type);
        const size_t size = VmmSlabSizes[index];

        const uintptr_t base = PMM::Global().Alloc(VmmMetaSlabPages) + hhdmBase;
        const size_t totalSpace = VmmMetaSlabPages * PageSize;
        const uintptr_t usableBase = sl::AlignUp(base + sizeof(VmmMetaSlab), size);
        const size_t usableSpace = totalSpace - (usableBase - base);

        size_t slabCount = usableSpace / size;
        size_t bitmapBytes = sl::AlignUp(slabCount, 8) / 8;
        while ((slabCount * size + bitmapBytes > usableSpace) && (slabCount > 0))
        {
            slabCount--;
            bitmapBytes = sl::AlignUp(slabCount, 8) / 8;
        }
        ASSERT(slabCount > 0, "Bad VMM meta slab params");

        VmmMetaSlab* slab = new(reinterpret_cast<void*>(base)) VmmMetaSlab();
        slab->next = metaSlabs[index];
        slab->total = slabCount;
        slab->free = slabCount;
        slab->data = usableBase;
        slab->bitmap = reinterpret_cast<uint8_t*>(usableBase + slabCount * size);

        sl::memset(slab->bitmap, 0, bitmapBytes);
        metaSlabs[index] = slab;

        Log("VMM metadata slab created: type=%lu, %lu entries + %lub early slack",
            LogLevel::Verbose, index, slabCount, totalSpace - usableSpace);

        return slab;
    }

    void* VMM::AllocMeta(VmmMetaType type)
    {
        const size_t slabIndex = static_cast<size_t>(type);
        const size_t slabSize = VmmSlabSizes[slabIndex];

        sl::ScopedLock allocLock(metaSlabLocks[slabIndex]);
        VmmMetaSlab* slab = metaSlabs[slabIndex];
        while (slab != nullptr && slab->free == 0)
            slab = slab->next;

        if (slab == nullptr)
        {
            slab = CreateMetaSlab(type);
            VALIDATE(slab != nullptr, nullptr, "VMM meta slab creation failed");
        }

        const size_t found = sl::BitmapFindClear(slab->bitmap, slab->total);
        ASSERT(found != slab->total, "VMM meta slab exhausted");
        sl::BitmapSet(slab->bitmap, found);
        slab->free--;

        return reinterpret_cast<void*>(slab->data + (found * slabSize));
    }

    void VMM::FreeMeta(void* ptr, VmmMetaType type)
    {
        const size_t slabIndex = static_cast<size_t>(type);
        const size_t slabSize = VmmSlabSizes[slabIndex];

        sl::ScopedLock allocLock(metaSlabLocks[slabIndex]);
        VmmMetaSlab* slab = metaSlabs[slabIndex];

        while (slab != nullptr)
        {
            const uintptr_t top = slab->data + (slab->total * slabSize);
            if ((uintptr_t)ptr < slab->data || (uintptr_t)ptr > top)
            {
                slab = slab->next;
                continue;
            }

            //found the owning slab.
            const size_t index = ((uintptr_t)ptr - slab->data) / slabSize;
            sl::BitmapClear(slab->bitmap, index);
            return;
        }

        ASSERT_UNREACHABLE() //means the metadata wasnt allocated from one of our slabs.
    }

    void VMM::AdjustHole(VmHole* target, size_t offset, size_t length)
    {
        ASSERT(offset + length <= target->length, "AdjustHole() bad params")

        holes.Remove(target);

        //if we're taking a chunk out of the middle of the hole, preserve the address
        //space before the chunk we're taking.
        if (offset > 0)
        {
            VmHole* hole = new(AllocMeta(VmmMetaType::Hole)) VmHole();
            hole->base = target->base;
            hole->length = offset;
            holes.Insert(hole);
        }

        //check if there's any usable space after this allocation to preserve.
        if (offset + length < target->length)
        {
            VmHole* hole = new(AllocMeta(VmmMetaType::Hole)) VmHole();
            hole->base = target->base + offset + length;
            hole->length = target->length - (offset + length);
            holes.Insert(hole);
        }
    }

    VmRange* VMM::FindRange(uintptr_t addr)
    {
        if (addr < globalLowerBound || addr >= globalUpperBound)
            return nullptr;

        sl::ScopedLock rangeTreeLock(rangesLock);
        VmRange* scan = ranges.GetRoot();
        while (scan != nullptr)
        {
            if (addr >= scan->base && addr < scan->Top())
                return scan;

            if (addr < scan->base)
                scan = ranges.GetLeft(scan); 
            else if (addr >= scan->Top())
                scan = ranges.GetRight(scan);
        }

        return nullptr;
    }
    
    sl::Lazy<VMM> kernelVmm;
    void VMM::InitKernel()
    {
        HatInit(); //arch-specific setup of the MMU.
        Virtual::VmDriver::InitAll(); //bring up basic VM drivers, including kernel driver.
        kernelVmm.Init(VmmKey{}); //VmmKey calls the constructor for the kernel vmm.
        Heap::Global().Init();
    }

    VMM& VMM::Kernel()
    { return *kernelVmm; }

    VMM& VMM::Current()
    { return *static_cast<VMM*>(CoreLocal()[LocalPtr::Vmm]); }

    VMM::VirtualMemoryManager()
    {
        sl::ScopedLock scopeLock(rangesLock);

        globalLowerBound = PageSize; //dont allocate in page 0.
        globalUpperBound = ~hhdmBase - PageSize;
        hatMap = InitNewMap();

        for (size_t i = 0; i < (size_t)VmmMetaType::Count; i++)
            metaSlabs[i] = nullptr;

        VmHole* initialHole = new(AllocMeta(VmmMetaType::Hole)) VmHole();
        initialHole->base = globalLowerBound;
        initialHole->length = globalUpperBound - globalLowerBound;
        initialHole->largestHole = initialHole->length;
        holes.Insert(initialHole);

        Log("User VMM initialized.", LogLevel::Info);
    }

    VMM::VirtualMemoryManager(VmmKey)
    {
        sl::ScopedLock scopeLock(rangesLock);

        //protect the HHDM from allocations, as well as the kernel binary.
        globalLowerBound = hhdmBase + hhdmLength;
        globalUpperBound = sl::AlignDown((uintptr_t)KERNEL_BLOB_BEGIN, GetHatLimits().modes[0].granularity);

        for (size_t i = 0; i < (size_t)VmmMetaType::Count; i++)
            metaSlabs[i] = nullptr;

        VmHole* initialHole = new(AllocMeta(VmmMetaType::Hole)) VmHole(); //TODO: extract this + user level version into a common_vmm_init() func
        initialHole->base = globalLowerBound;
        initialHole->length = globalUpperBound - globalLowerBound;
        initialHole->largestHole = initialHole->length;
        holes.Insert(initialHole);

        hatMap = KernelMap();
        MakeActiveMap(hatMap);

        const size_t usableSpace = globalUpperBound - globalLowerBound;
        auto conv = sl::ConvertUnits(usableSpace);
        Log("Kernel VMM bootstrap: %lu.%lu%sB usable space, base=0x%lx.",
            LogLevel::Info, conv.major, conv.minor, conv.prefix, globalLowerBound);
    }

    VMM::~VirtualMemoryManager()
    {
        ASSERT_UNREACHABLE(); //TODO: vmm teardown
    }

    void VMM::MakeActive()
    {
        if (hatMap != KernelMap())
            SyncWithMasterMap(hatMap);
        MakeActiveMap(hatMap);
    }

    bool VMM::HandleFault(uintptr_t addr, VmFaultFlags flags)
    {
        if (addr < globalLowerBound || addr >= globalUpperBound)
            return false;
        
        //determine if this is a good or bad page fault by trying to locate a range
        //containing the faulting address.
        VmRange* range = FindRange(addr);
        if (range == nullptr)
            return false;

        using namespace Virtual;
        VmDriver* driver = VmDriver::GetDriver(range->flags);
        VALIDATE(driver != nullptr, false, "VmRange exists without a known driver");

        VmDriverContext context { .lock = mapLock, .map = hatMap, .range = *range };
        const EventResult result = driver->HandleFault(context, addr, flags);
        if (!result.goodFault)
            return false;

        Log("Good page fault: addr=0x%lx, ec=0x%lx", LogLevel::Debug, addr, flags.Raw());
        return true;
    }

    sl::Opt<uintptr_t> VMM::Alloc(size_t length, uintptr_t initArg, VmFlags flags, VmmAllocLimits limits)
    {
        using namespace Virtual;

        //before even thinking about allocating address space, find the driver used to
        //back this type of memory and find out what it would need to fulfill it.
        VmDriver* driver = VmDriver::GetDriver(flags);
        if (driver == nullptr)
            return {};

        const QueryResult query = driver->Query(length, flags, initArg);
        if (!query.success)
            return {};

        sl::ScopedLock holyLock(holesLock);
        VmHole* hole = holes.GetRoot();
        VALIDATE(hole != nullptr, {}, "No address space holes?");

        //TODO: respect alignment when allocating + limits
        uintptr_t rangeBase = 0;
        while (true)
        {
            if (VmHole* left = holes.GetLeft(hole); 
                left != nullptr && left->largestHole >= query.length)
            {
                //there's a hole with a lower address and it (or one of its children)
                //has enough space for the allocation.
                hole = left;
                continue;
            }

            //check if the current hole can meet the requirements
            if (hole->length >= query.length)
            {
                rangeBase = hole->base;
                AdjustHole(hole, 0, query.length);
                FreeMeta(hole, VmmMetaType::Hole);
                hole = nullptr;
                break;
            }
            
            //all else failed, look at the next hole higher up the address space
            hole = holes.GetRight(hole);
            VALIDATE(hole != nullptr, {}, "No hole in address space large enough.");
            VALIDATE(hole->largestHole >= query.length, {}, "No hole in address space large enough.");
        }
        holyLock.Release();
        VALIDATE(rangeBase != 0, {}, "Will not allocate VM at address 0");

        VmRange* vmRange = new(AllocMeta(VmmMetaType::Range)) VmRange();
        vmRange->base = rangeBase;
        vmRange->length = query.length;
        vmRange->flags = flags;

        //we've reserved some of the address space, now attach a vmdriver to this
        //range of memory.
        VmDriverContext context { .lock = mapLock, .map = hatMap, .range = *vmRange };
        const AttachResult attachResult = driver->Attach(context, query, initArg);
        if (!attachResult.success)
        {
            VmHole* returnHole = new(AllocMeta(VmmMetaType::Hole)) VmHole();
            returnHole->base = vmRange->base;
            returnHole->length = vmRange->length;
            holesLock.Lock();
            holes.Insert(returnHole);
            holesLock.Unlock();

            FreeMeta(vmRange, VmmMetaType::Range);
            return {};
        };

        vmRange->token = attachResult.token;
        vmRange->offset = attachResult.offset;
        
        rangesLock.Lock();
        ranges.Insert(vmRange);
        rangesLock.Unlock();

        return vmRange->base + vmRange->offset;
    }

    bool VMM::Free(uintptr_t base)
    {
        #warning "Fix VMM::Free memory corruption"
        VmRange* range = FindRange(base);
        if (range == nullptr)
            return false;

        rangesLock.Lock();
        ranges.Remove(range);
        rangesLock.Unlock();

        using namespace Virtual;
        VmDriver* driver = VmDriver::GetDriver(range->flags);
        VALIDATE(driver != nullptr, false, "Active VmRange with no known driver");

        VmDriverContext context { .lock = mapLock, .map = hatMap, .range = *range};
        const bool detachSuccess = driver->Detach(context);
        if (!detachSuccess)
        {
            Log("%s vmdriver failed to detach range @ 0x%lx, 0x%lx bytes.",
                LogLevel::Warning, VmDriver::GetName(range->flags), range->base, range->length);
            FreeMeta(range, VmmMetaType::Range);
            return false;
        }

        VmHole* hole = new(AllocMeta(VmmMetaType::Hole)) VmHole();
        hole->length = range->length;
        hole->base = range->base;

        FreeMeta(range, VmmMetaType::Range);
        holesLock.Lock();
        holes.Insert(hole);
        holesLock.Unlock();
        return true;
    }

    sl::Opt<VmFlags> VMM::GetFlags(uintptr_t base, size_t length)
    {
        const VmRange* range = FindRange(base);
        if (range == nullptr || range->Top() < base + length)
            return {};

        return range->flags;
    }

    bool VMM::SetFlags(uintptr_t base, size_t length, VmFlags flags)
    {
        VmRange* range = FindRange(base);
        if (range == nullptr || range->Top() < base + length)
            return false;

        using namespace Virtual;
        VmDriver* driver = VmDriver::GetDriver(range->flags);
        if (driver == nullptr)
            return false;

        VmDriverContext context { .lock = mapLock, .map = hatMap, .range = *range };
        return driver->ModifyRange(context, flags);
    }

    bool VMM::MemoryExists(uintptr_t base, size_t length, sl::Opt<VmFlags> flags)
    {
        const VmRange* range = FindRange(base);
        if (range == nullptr)
            return false;
        if (range->Top() < base + length)
            return false;

        if (flags.HasValue())
            return (*flags & range->flags) == *flags;
        return true;
    }

    sl::Opt<uintptr_t> VMM::GetPhysical(uintptr_t vaddr)
    {
        return GetMap(hatMap, vaddr);
    }

    size_t VMM::GetDebugData(sl::Span<VmmDebugEntry>& entries, size_t offset)
    {
        ASSERT_UNREACHABLE();
    }

    size_t VMM::CopyIn(void* foreignBase, void* localBase, size_t length)
    {
        if (!MemoryExists((uintptr_t)foreignBase, length, {}))
            return 0;

        sl::NativePtr local = localBase;
        size_t count = 0;

        while (count < length)
        {
            auto maybePhys = GetMap(hatMap, (uintptr_t)foreignBase + count);
            if (!maybePhys.HasValue())
            {
                HandleFault((uintptr_t)foreignBase + count, VmFaultFlag::Write);
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
