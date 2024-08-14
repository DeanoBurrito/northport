#include <memory/Vmm.h>
#include <memory/virtual/VmDriver.h>
#include <memory/virtual/KernelVmDriver.h>
#include <memory/Pmm.h>
#include <memory/Heap.h>
#include <tasking/Threads.h>
#include <arch/Hat.h>
#include <boot/LinkerSyms.h>
#include <debug/Log.h>
#include <Bitmap.h>
#include <Memory.h>
#include <Lazy.h>
#include <Maths.h>
#include <UnitConverter.h>

namespace Npk::Memory
{
    //The VmHoleAggregator functions are more or less copied from managarm, thanks
    //to the team there.
    bool VmHoleAggregator::Aggregate(VmHole* hole)
    {
        size_t size = hole->length;
        const VmHole* left = VmHoleTree::GetLeft(hole);
        const VmHole* right = VmHoleTree::GetRight(hole);
        if (left != nullptr && left->largestHole > size)
            size = left->largestHole;
        if (right != nullptr && right->largestHole > size)
            size = right->largestHole;

        if (hole->largestHole == size)
            return false;
        hole->largestHole = size;
        return true;
    }

    bool VmHoleAggregator::CheckInvariant(VmHoleTree& tree, VmHole* hole)
    {
        const VmHole* pred = tree.Predecessor(hole);
        const VmHole* succ = tree.Successor(hole);

        size_t size = hole->length;
        const VmHole* left = VmHoleTree::GetLeft(hole);
        const VmHole* right = VmHoleTree::GetRight(hole);
        if (left != nullptr && left->largestHole > size)
            size = left->largestHole;
        if (right != nullptr && right->largestHole > size)
            size = right->largestHole;

        if (hole->largestHole != size)
        {
            Log("VmHoleTree has invalid state!", LogLevel::Error);
            return false;
        }

        if (pred != nullptr && hole->base < pred->base + pred->length)
        {
            Log("VmHoleTree overlapping regions", LogLevel::Error);
            return false;
        }
        if (succ != nullptr && hole->base + hole->length > succ->base)
        {
            Log("VmHoleTree overlapping regions", LogLevel::Error);
            return false;
        }

        return true;
    }

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

        Log("VMM metadata slab created: type=%zu, %zu entries + %zub early slack",
            LogLevel::Verbose, index, slabCount, totalSpace - usableSpace);

        return slab;
    }

    bool VMM::DestroyMetaSlab(VmmMetaType type)
    {
        //return if there are more slabs of this type
        ASSERT_UNREACHABLE(); (void)type;
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

    void VMM::CommonInit()
    {
        //initialize meta allocators
        for (size_t i = 0; i < (size_t)VmmMetaType::Count; i++)
            metaSlabs[i] = nullptr;

        //Create the initial VM hole representing the initial address space
        VmHole* initialHole = new(AllocMeta(VmmMetaType::Hole)) VmHole();
        initialHole->base = globalLowerBound;
        initialHole->length = globalUpperBound - globalLowerBound;
        initialHole->largestHole = initialHole->length;
        holes.Insert(initialHole);
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
    
    VMM* kernelVmm;
    void VMM::InitKernel()
    {
        HatInit(); //arch-specific setup of the MMU.
        Virtual::VmDriver::InitAll(); //Bring-up VM drivers
        kernelVmm = &Tasking::Process::Kernel().vmm;
        new(kernelVmm) VMM(VmmKey{});
    }

    VMM& VMM::Kernel()
    { return *kernelVmm; }

    VMM& VMM::Current()
    { return *static_cast<VMM*>(CoreLocal()[LocalPtr::UserVmm]); }

    bool VMM::CurrentActive()
    { return CoreLocal()[LocalPtr::UserVmm] != nullptr; }

    VMM::VirtualMemoryManager()
    {
        sl::ScopedLock scopeLock(rangesLock);

        /* User VMMs managed (almost) the entire lower half of an address space, with
         * a small chunk reserved at either size. HAT mode 0 is the smallest mode support,
         * usually the page size (if that metric makes sense for the architecture).
         * We leave a gap of `mode0.granularity` bytes on either side of the address
         * space the VMM manages.
         * - The space at the beginning is so that address 0 (NULL/nullptr in many languages)
         *   is unmapped, resulting in a bad page fault on null pointer dereferences.
         *   A nice side affect of keeping a few additional bytes unmapped is that this
         *   will also fault on accesses to member accesses to objects with null as their address.
         * - The space at the top of the managed area is to prevent a bug on some platforms.
         *   This bug allows userspace to cause the kernel to trigger a fault, on behalf of
         *   the user code, which can lead to some bad things. It's documented by the Fuchsia
         *   team at: https://fuchsia.dev/fuchsia-src/concepts/kernel/sysret_problem
         */
        const size_t granuleSize = HatGetLimits().modes[0].granularity;
        globalLowerBound = granuleSize;
        globalUpperBound = ~hhdmBase - granuleSize;

        CommonInit();
        hatMap = HatCreateMap();

        const size_t usableSpace = globalUpperBound - globalLowerBound;
        auto conv = sl::ConvertUnits(usableSpace, sl::UnitBase::Binary);
        Log("User VMM created: %zu.%zu%sB usable space, base=0x%tx.", LogLevel::Info,
            conv.major, conv.minor, conv.prefix, globalLowerBound);
    }

    VMM::VirtualMemoryManager(VmmKey)
    {
        sl::ScopedLock scopeLock(rangesLock);

        /* The higher half of the address space is split into 3 main blocks:
         * - at the base is the hhdm, from hhdmBase -> hhdmBase + hhdmLength.
         * - the topmost 2GiB are reserved for the kernel binary.
         * - everything in between is managed by the kernel VMM.
         */
        globalLowerBound = hhdmBase + hhdmLength;
        globalUpperBound = sl::AlignUp((uintptr_t)KERNEL_BLOB_BEGIN + (uintptr_t)KERNEL_BLOB_SIZE, 
            HatGetLimits().modes[0].granularity);

        CommonInit();
        hatMap = KernelMap();
        HatMakeActive(hatMap, true);

        auto kernelRanges = Virtual::GetKernelRanges();
        for (size_t i = 0; i < kernelRanges.Size(); i++)
            ranges.Insert(&kernelRanges[i]);

        const size_t usableSpace = globalUpperBound - globalLowerBound;
        auto conv = sl::ConvertUnits(usableSpace, sl::UnitBase::Binary);
        Log("Kernel VMM bootstrap: %zu.%zu%sB usable space, base=0x%tx.",
            LogLevel::Info, conv.major, conv.minor, conv.prefix, globalLowerBound);
    }

    VMM::~VirtualMemoryManager()
    {
        ASSERT(hatMap != KernelMap(), "Attempted to destroy kernel VMM.");

        //Not strictly necessary, but better to know we're not using the VMM we're
        //about to destroy. Switch to the kernel map.
        HatMakeActive(KernelMap(), true);
        
        //iterate through active ranges, detaching each one and freeing the backing memory.
        //This also frees the VM range structs as well.
        while (VmRange* range = ranges.GetRoot())
        {
            if (!Free(range->base))
                Log("Failed to destroy VM range during teardown.", LogLevel::Warning);
        }

        //free memory used by VM hole structs
        size_t holesFreed = 0;
        while (VmHole* hole = holes.GetRoot())
        {
            holes.Remove(hole);
            holesFreed++;
            FreeMeta(hole, VmmMetaType::Hole);
        }
        if (holesFreed > 1)
        {
            Log("Multiple VmHoles freed from end-of-life VMM, missing virtual memory?",
                LogLevel::Warning);
        }

        //Free memory used by meta allocators
        while (DestroyMetaSlab(VmmMetaType::Range)) {}
        while (DestroyMetaSlab(VmmMetaType::Hole)) {}

        //cleanup memory used by HAT structures, no need to lock as no one else
        //can access the VMM at this stage in its life.
        HatDestroyMap(hatMap);
        hatMap = nullptr;
    }

    void VMM::MakeActive()
    {
        HatMakeActive(hatMap, globalUpperBound > hhdmBase);
    }

    bool VMM::HandleFault(uintptr_t addr, VmFaultFlags flags)
    {
        if (addr < globalLowerBound || addr >= globalUpperBound)
            return false;
        stats.faults++;
        
        //determine if this is a good or bad page fault by trying to locate a range
        //containing the faulting address.
        VmRange* range = FindRange(addr);
        if (range == nullptr)
            return false;

        //determine if access is legal according to VM range flags. The fault may
        //be due to a VM driver not passing on the full flags to the HAT.
        if (flags.Has(VmFaultFlag::Write) && !range->flags.Has(VmFlag::Write))
            return false;
        if (flags.Has(VmFaultFlag::Execute) && !range->flags.Has(VmFlag::Execute))
            return false;
        if (flags.Has(VmFaultFlag::User) && !range->flags.Has(VmFlag::User))
            return false;

        //locate the attached driver, and inform it of the fault.
        using namespace Virtual;
        VmDriver* driver = VmDriver::GetDriver(range->flags);
        VALIDATE(driver != nullptr, false, "VmRange exists without a known driver");

        VmDriverContext context { .lock = mapLock, .map = hatMap, .range = *range, .stats = stats };
        const EventResult result = driver->HandleFault(context, addr, flags);

        return result.goodFault;
    }

    VmmStats VMM::GetStats() const
    {
        return stats;
    }

    sl::Opt<uintptr_t> VMM::Alloc(size_t length, uintptr_t initArg, VmFlags flags, VmAllocLimits limits)
    {
        /* Allocating has a few main steps:
         * - First find the driver for the requested type of memory.
         * - Call driver.Query() to get specifics on what the VM driver would need to
         *   actually attach backing to the VM range. This includes the real number of bytes
         *   required to map the requested number of bytes, alignment and the HAT mode.
         * - Find a hole in the address space that meets the query criteria.
         * - Split the found VM hole and create a VM range struct to represent the removed
         *   address space. Store some metadata like the flags here too.
         * - Attach the VM driver to the freshly created VM range. Conceptually the virtual
         *   memory is now ready for use, although that doesn't necessarily mean it's
         *   backed yet. That happens at the discretion of the attached VM driver.
         * - Store the token returned by the VM driver in the range struct, and finally
         *   stash the VM range in the global list/tree so it's available to the
         *   rest of the VMM.
         */
        if (length == 0)
            return {};

        using namespace Virtual;
        VmDriver* driver = VmDriver::GetDriver(flags);
        if (driver == nullptr)
            return {};

        //query the driver, find out what it would require to satify this allocation
        const QueryResult query = driver->Query(length, flags, initArg); //TODO: tell vmdriver about alignment request
        if (!query.success)
            return {};
        if (limits.alignment > 1 && (query.alignment % limits.alignment) != 0)
            return {};

        sl::ScopedLock holyLock(holesLock);
        VmHole* hole = holes.GetRoot();
        VALIDATE(hole != nullptr, {}, "No address space holes?");

        //find an empty part of the address that meets our criteria.
        uintptr_t rangeBase = 0;
        while (true)
        {
            //TODO: respect upper + lower bounds + alignment
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

        //create VM range struct, start populating it
        VmRange* vmRange = new(AllocMeta(VmmMetaType::Range)) VmRange();
        vmRange->base = rangeBase;
        vmRange->length = query.length;
        vmRange->flags = flags;
        vmRange->mdlCount = 0;

        //attach the VM driver to this range, store the offset and token.
        VmDriverContext context { .lock = mapLock, .map = hatMap, .range = *vmRange, .stats = stats };
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
        
        //make range known to the rest of the VMM.
        rangesLock.Lock();
        ranges.Insert(vmRange);
        rangesLock.Unlock();

        if (flags.Has(VmFlag::Anon))
            stats.anonRanges++;
        else if (flags.Has(VmFlag::File))
            stats.fileRanges++;
        else if (flags.Has(VmFlag::Mmio))
            stats.mmioRanges++;

        return vmRange->base + vmRange->offset;
    }

    bool VMM::Free(uintptr_t base)
    {
        /* Freeing virtual memory is the reverse of allocating:
         * - First the range struct is removed from the list, meaning that part of the
         *   address space isn't available for allocations (it's not in the holes list)
         *   and its also not an active range, and wont be considered for faults or other
         *   lookups.
         * - The attached VM driver is located, and driver.Detach is called. This is where
         *   the driver can cleanup any backing memory, populate dirty bits etc. If this function
         *   fails the space represented by the VM range is simply dropped, and not merged into
         *   the VM holes tree. This is because the exact state of the virtual memory is unknown,
         *   there could be something mapped still, or parts of something mapped. Rather than
         *   risk a class of bugs that could come from trying to re-allocate over it, the
         *   space is simply dropped - essentially leaking the virtual memory.
         * - If driver.Detach succeeds, the memory represented by the range is merged into the
         *   VM holes tree, and the VM range is freed. Now the virtual memory is completely freed,
         *   and ready for other uses.
         */

        VmRange* range = FindRange(base);
        if (range == nullptr)
            return false;
        VALIDATE(range->mdlCount == 0, false, "Cannot free VM range with mdlCount > 0 (it's still in use)");

        rangesLock.Lock();
        ranges.Remove(range);
        rangesLock.Unlock();

        if (range->flags.Has(VmFlag::Anon))
            stats.anonRanges--;
        else if (range->flags.Has(VmFlag::File))
            stats.fileRanges--;
        else if (range->flags.Has(VmFlag::Mmio))
            stats.mmioRanges--;

        using namespace Virtual;
        VmDriver* driver = VmDriver::GetDriver(range->flags);
        VALIDATE(driver != nullptr, false, "Active VmRange with no known driver");

        VmDriverContext context { .lock = mapLock, .map = hatMap, .range = *range, .stats = stats };
        const bool detachSuccess = driver->Detach(context);
        if (!detachSuccess)
        {
            Log("%s vmdriver failed to detach range @ 0x%tx, 0x%zx bytes.",
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

    bool VMM::SetFlags(uintptr_t base, VmFlags flags)
    {
        VmRange* range = FindRange(base);
        if (range == nullptr)
            return false;
        if ((flags.Raw() & VmFlagTypeMask) != 0)
            return false; //we dont support modifying the type of an existing range

        using namespace Virtual;
        VmDriver* driver = VmDriver::GetDriver(range->flags);
        if (driver == nullptr)
            return false;
        flags &= ~VmFlagTypeMask;

        VmDriverContext context { .lock = mapLock, .map = hatMap, .range = *range, .stats = stats };
        ModifyRangeArgs args {};
        args.setFlags = flags.Raw() & ~range->flags.Raw();
        args.clearFlags = range->flags.Raw() & ~flags.Raw();

        if (!driver->ModifyRange(context, args))
            return false;

        range->flags = (range->flags & VmFlagTypeMask) | flags;
        return true;
    }

    sl::Opt<uintptr_t> VMM::Split(uintptr_t base, uintptr_t offset)
    {
        VmRange* range = FindRange(base);
        if (range == nullptr)
            return {};
        VALIDATE(range->mdlCount == 0, {}, "Cannot split VM range with mdlCount > 0");

        offset += base - (range->base + range->offset);
        using namespace Virtual;
        VmDriver* driver = VmDriver::GetDriver(range->flags);
        if (driver == nullptr)
            return {};

        VmDriverContext context { .lock = mapLock, .map = hatMap, .range = *range, .stats = stats };
        const SplitResult result = driver->Split(context, offset);
        if (!result.success)
            return {};

        if (result.offset == range->length)
            return range->base + offset; //effectively a no-op

        VmRange* newRange = new(AllocMeta(VmmMetaType::Range)) VmRange();

        newRange->base = range->base + result.offset;
        newRange->length = range->length - result.offset;
        newRange->flags = range->flags;
        newRange->offset = 0;
        newRange->token = result.tokenHigh;
        newRange->mdlCount = 0;

        range->length = newRange->base - range->base;
        range->token = result.tokenLow;

        if (range->flags.Has(VmFlag::Anon))
            stats.anonRanges++;
        else if (range->flags.Has(VmFlag::File))
            stats.fileRanges++;
        else if (range->flags.Has(VmFlag::Mmio))
            stats.mmioRanges++;

        rangesLock.Lock();
        ranges.Insert(newRange);
        rangesLock.Unlock();

        return result.offset - range->offset;
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
        size_t ignored;
        return HatGetMap(hatMap, vaddr, ignored);
    }

    size_t VMM::CopyIn(void* foreignBase, void* localBase, size_t length)
    {
        if (!MemoryExists((uintptr_t)foreignBase, length, {}))
            return 0;

        sl::NativePtr local = localBase;
        size_t count = 0;

        while (count < length)
        {
            size_t ignored;
            auto maybePhys = HatGetMap(hatMap, (uintptr_t)foreignBase + count, ignored);
            if (!maybePhys.HasValue())
            {
                if (!HandleFault((uintptr_t)foreignBase + count, VmFaultFlag::Write))
                    return count;
                maybePhys = HatGetMap(hatMap, (uintptr_t)foreignBase + count, ignored);
            }
            
            size_t copyLength = sl::Min(PageSize, length - count);
            //first copy can be misaligned (in the destination address space), so handle that.
            if (count == 0)
                copyLength = sl::Min(copyLength, PageSize - ((uintptr_t)foreignBase % PageSize));

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
            size_t ignored;
            auto maybePhys = HatGetMap(hatMap, (uintptr_t)foreignBase + count, ignored);
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

    sl::Opt<Mdl> VMM::AcquireMdl(uintptr_t base, size_t length)
    {
        VmRange* range = FindRange(base);
        if (range == nullptr || length == 0)
            return {};
        length = sl::Min(base + length, range->base + range->length) - base;

        //TODO: store mdlCount in the smallest unit possible (page or group-of-pages level) instead of pinning the entire range.
        range->mdlCount++;

        //create the mdl list
        sl::Vector<MdlPtr> ptrs;
        for (size_t scan = base; scan < base + length;)
        {
            size_t mode = 0;

            //get physical memory or fault it in if necessary
            auto maybeMap = HatGetMap(hatMap, scan, mode);
            if (!maybeMap.HasValue())
            {
                ASSERT_(HandleFault(scan, VmFaultFlag::Write));
                maybeMap = HatGetMap(hatMap, scan, mode);
            }

            ASSERT_(maybeMap.HasValue());
            auto& ptr = ptrs.EmplaceBack();
            ptr.physAddr = *maybeMap;
            ptr.length = sl::Min(HatGetLimits().modes[mode].granularity, length + base - scan);

            scan += ptr.length;
        }

        Mdl mdl {};
        mdl.base = base;
        mdl.length = length;
        mdl.vmm = this;
        mdl.ptrs = sl::Move(ptrs);

        return mdl;
    }

    void VMM::ReleaseMdl(uintptr_t base)
    {
        VmRange* range = FindRange(base);
        if (range == nullptr)
            return;
        ASSERT_(range->mdlCount > 0);

        range->mdlCount--;
    }
}
