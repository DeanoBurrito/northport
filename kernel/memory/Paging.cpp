#include <memory/PhysicalMemory.h>
#include <memory/Paging.h>
#include <Memory.h>
#include <scheduling/Thread.h>
#include <boot/Limine.h>
#include <boot/LinkerSymbols.h>
#include <Utilities.h>
#include <Platform.h>
#include <arch/Cpu.h>
#include <Log.h>
#include <Maths.h>
#include <Locks.h>

namespace Kernel::Memory
{   
    /*  Notes about current implementation:
            - Supports 4 and 5 level paging
            - DOES NOT map pages it allocates for page tables
            - Page table entries use generic 'writes enabled, present' flags. Requested flags only apply to the final entry.
    */
    
    enum PageEntryFlag : uint64_t
    {
        None = 0,
        
        //set to make this entry used in translation
        Present = 1 << 0,
        //set if memory covered by this entry is read/write. Clear for read-only
        RegionWritesAllowed = 1 << 1,
        //set if usermode accesses are allowed through this entry. Cleared for only non-usermode accesses.
        UserAccessAllowed = 1 << 2,
        //MTRR stuff, cleared by default
        CacheWriteThrough = 1 << 3,
        //MTRR stuff, cleared by default
        CacheDisable = 1 << 4,
        //set if this entry has been used in a translation since last clear
        Accessed = 1 << 5,
        //reserved if PageSize is not set. Set when a write has occured through this entry.
        Dirty = 1 << 6,
        //tells translation to end at this level, and use the remaining address as the physical page.
        //set this at level 2 for 2MB pages, level 3 for 1GB pages. (check for support first)
        PageSize = 1 << 7,
        //if enabled in EFER, makes this translation global
        IsGlobal = 1 << 8,

        //these are ignored by the cpu across all entries, and are available to use.
        Custom0 = 1 << 9,
        Custom1 = 1 << 10,
        Custom2 = 1 << 11,

        //reserved if PageSize is not set. MTRR stuff, cleared by default.
        PAT = 1 << 12,
        //if enabled in EFER, disabled instruction fetches through this entry
        ExecuteDisable = 1ul << 63,
    };

    FORCE_INLINE PageEntryFlag operator |(PageEntryFlag a, PageEntryFlag b)
    { return static_cast<PageEntryFlag>((uint64_t)a | (uint64_t)b); }

    FORCE_INLINE PageEntryFlag operator ~(PageEntryFlag a)
    { return static_cast<PageEntryFlag>(~(uint64_t)a); }
    
    struct PageTableEntry
    {
        uint64_t raw;

        PageTableEntry(uint64_t data) : raw(data)
        {}

        void SetFlag(PageEntryFlag flagMask)
        {
            raw |= flagMask & 0xFFF0'0000'0000'0FFF; //TODO: this does not support 57-bit address!
        }

        void ClearFlag(PageEntryFlag flagMask)
        {
            raw &= ~flagMask & 0xFFF0'0000'0000'0FFF;
        }

        bool HasFlag(PageEntryFlag flag)
        {
            return (raw & (flag & 0xFFF0'0000'0000'0FFF)) != 0;
        }

        void SetAddr(sl::NativePtr address)
        {
            //zero address bits in raw entry, then move address to offset, and preserve control bits.
            raw &= 0xFFF0'0000'0000'0FFF;
            raw |= address.raw & ~(0xFFF0'0000'0000'0FFF);
        }

        sl::NativePtr GetAddr()
        {
            return raw & ~(0xFFF0'0000'0000'0FFF);
        }
    };

    struct PageTable
    {
        PageTableEntry entries[512];
    };

    PageEntryFlag GetPageEntryFlags(MemoryMapFlags flags)
    {
        PageEntryFlag finalFlags = PageEntryFlag::None;

        if (sl::EnumHasFlag(flags, MemoryMapFlags::AllowWrites))
            finalFlags = finalFlags | PageEntryFlag::RegionWritesAllowed;
        if (sl::EnumHasFlag(flags, MemoryMapFlags::UserAccessible))
            finalFlags = finalFlags | PageEntryFlag::UserAccessAllowed;
        if (CPU::FeatureSupported(CpuFeature::ExecuteDisable) && !sl::EnumHasFlag(flags, MemoryMapFlags::AllowExecute))
            finalFlags = finalFlags | PageEntryFlag::ExecuteDisable;

        return finalFlags;
    }

    FORCE_INLINE 
    void GetPageTableIndices(sl::NativePtr virtAddr, size_t* indices)
    {
        indices[5] = (virtAddr.raw >> 48) & 0x1FF;
        indices[4] = (virtAddr.raw >> 39) & 0x1FF;
        indices[3] = (virtAddr.raw >> 30) & 0x1FF;
        indices[2] = (virtAddr.raw >> 21) & 0x1FF;
        indices[1] = (virtAddr.raw >> 12) & 0x1FF;
    }

    FORCE_INLINE
    void PageTableManager::InvalidatePage(sl::NativePtr virtualAddress) const
    {
        asm volatile("invlpg %0" :: "m"(virtualAddress.ptr));
    }

    PageTableManager defaultPageTableManager;
    PageTableManager* PageTableManager::Current()
    {
        if (ReadMsr(MSR_GS_BASE) == 0 || CoreLocal()->ptrs[CoreLocalIndices::CurrentThread].ptr == nullptr)
            return &defaultPageTableManager;
        else 
            return &Scheduling::Thread::Current()->Parent()->VMM()->pageTables; //ah oops... its starting to look like LINQ
    }

    size_t PageTableManager::pagingLevels;
    void PageTableManager::Setup()
    {
        Log("Beginning system paging setup for pre-scheduler kernel.", LogSeverity::Info);

        if (CPU::FeatureSupported(CpuFeature::ExecuteDisable))
        {
            //ensure EFER.NX is enabled (might already be enabled from bootloader)
            uint64_t eferCurrent = ReadMsr(MSR_IA32_EFER);
            eferCurrent |= (1 << 11);
            WriteMsr(MSR_IA32_EFER, eferCurrent);

            Log("Execute disable is available, and enabled in EFER.", LogSeverity::Verbose);
        }
        else
            Log("Execute disable not available.", LogSeverity::Verbose);

        if (CPU::FeatureSupported(CpuFeature::GigabytePages))
            Log("Gigabyte pages are available.", LogSeverity::Verbose);

        uint64_t cr4 = ReadCR4();
        if ((cr4 & (1 << 12)) != 0)
        {
            pagingLevels = 5;
            Log("System is setup for 5-level paging.", LogSeverity::Verbose);
        }
        else
        {
            pagingLevels = 4;
            Log("System is setup for 4-level paging.", LogSeverity::Verbose);
        }

        Log("Paging setup successful.", LogSeverity::Info);
    }
    
    void PageTableManager::InitKernelFromLimine(bool reuseBootloaderMaps)
    {
        if (reuseBootloaderMaps)
            topLevelAddress = ReadCR3();
        else
        {
            topLevelAddress = PMM::Global()->AllocPage();
            sl::memset(topLevelAddress.ptr, 0, sizeof(PageTable));

            const uint64_t kernelBaseVirt = Boot::kernelAddrRequest.response->virtual_base;
            const uint64_t kernelBasePhys = Boot::kernelAddrRequest.response->physical_base;
            const auto memmap = Boot::memmapRequest.response;

            //utility function for mapping an entire kernel range, accounting for virt/phys skew
            auto MapKernelSection = [=](uint64_t base, uint64_t top, MemoryMapFlags flags)
            {
                const size_t pageCount = (top - base) / PAGE_FRAME_SIZE;
                const uint64_t physBase = (base - kernelBaseVirt) + kernelBasePhys;
                MapRange(base, physBase, pageCount, flags);
            };

            //first pass: map kernel binary into the new paging structure.
            MapKernelSection(
                (uint64_t)KERNEL_TEXT_BEGIN / PAGE_FRAME_SIZE * PAGE_FRAME_SIZE, 
                ((uint64_t)KERNEL_TEXT_END / PAGE_FRAME_SIZE + 1) * PAGE_FRAME_SIZE, 
                MemoryMapFlags::AllowExecute
            );
            MapKernelSection(
                (uint64_t)KERNEL_RODATA_BEGIN / PAGE_FRAME_SIZE * PAGE_FRAME_SIZE, 
                ((uint64_t)KERNEL_RODATA_END / PAGE_FRAME_SIZE + 1) * PAGE_FRAME_SIZE, 
                MemoryMapFlags::None
            );
            MapKernelSection(
                (uint64_t)KERNEL_DATA_BEGIN / PAGE_FRAME_SIZE * PAGE_FRAME_SIZE, 
                ((uint64_t)KERNEL_DATA_END / PAGE_FRAME_SIZE + 1) * PAGE_FRAME_SIZE, 
                MemoryMapFlags::AllowWrites
            );

            //second pass: map the first 4gb of memory unconditionally.
            for (size_t i = 0; i < 4 * GB; i += (size_t)PagingSize::_2MB)
                MapMemory(hhdmBase + i, i, PagingSize::_2MB, MemoryMapFlags::AllowWrites);
            hhdmLength = 4 * GB;

            //third pass: map any usable regions above 4gb
            for (size_t i = 0; i < memmap->entry_count; i++)
            {
                const limine_memmap_entry* region = memmap->entries[i];

                if (region->base + region->length < 4 * GB)
                    continue;
                if (region->type == LIMINE_MEMMAP_BAD_MEMORY)
                    continue;
                
                const uint64_t base = (region->base < 4 * GB) ? 4 * GB : region->base;
                const size_t regionTop = sl::min(region->length, HHDM_LIMIT - region->base);
                for (size_t j = 0; j < regionTop; j += PAGE_FRAME_SIZE)
                    MapMemory(hhdmBase + base + j, base + j, MemoryMapFlags::AllowWrites);

                hhdmLength = region->base + region->length;
                if (hhdmLength >= HHDM_LIMIT)
                {
                    Log("HHDM Limit (" MACRO_STR(HHDM_LIMIT) " bytes) has been hit, the upper reaches of physical memory may be unavailable.", LogSeverity::Warning);
                    hhdmLength = HHDM_LIMIT;
                    break;
                }
            }

        }

        MakeActive();
        Log("Kernel root page table initialized.", LogSeverity::Info);
    }

    void PageTableManager::InitClone()
    {
        sl::ScopedSpinlock scopeLock(&lock);

        topLevelAddress = PMM::Global()->AllocPage();
        sl::memset(EnsureHigherHalfAddr(topLevelAddress.ptr), 0, sizeof(PageTable));
        sl::memcopy(EnsureHigherHalfAddr(Current()->topLevelAddress.ptr), sizeof(PageTable) / 2, EnsureHigherHalfAddr(topLevelAddress.ptr), sizeof(PageTable) / 2, sizeof(PageTable) / 2);
        
        Log("Freshly cloned page table initialized.", LogSeverity::Verbose);
    }

    //helper function for PTM::Teardown()
    void FreePageTable(PageTable* pt, size_t level, bool includeHigherHalf)
    {
        const size_t ptEntryCount = 512;
        const size_t loopLimit = includeHigherHalf ? ptEntryCount : ptEntryCount / 2;
        
        for (size_t i = 0; i < loopLimit; i++)
        {
            if (!sl::EnumHasFlag(EnsureHigherHalfAddr(pt->entries[i].raw), PageEntryFlag::Present))
                continue;
            
            sl::NativePtr physAddr = pt->entries[i].raw & ~(0xFFFul | (1ul << 63));
            if (level == 1)
                PMM::Global()->FreePage(physAddr.ptr);
            else if ((level == 3 || level == 2) && sl::EnumHasFlag(EnsureHigherHalfAddr(pt->entries[i].raw), PageEntryFlag::PageSize))
            {
                if (level == 3)
                    PMM::Global()->FreePages(physAddr.ptr, (size_t)PagingSize::_1GB / PAGE_FRAME_SIZE);
                else
                    PMM::Global()->FreePages(physAddr.ptr, (size_t)PagingSize::_2MB / PAGE_FRAME_SIZE);
            }
            else
                FreePageTable(EnsureHigherHalfAddr(physAddr.As<PageTable>()), level - 1, true);
        }

        PMM::Global()->FreePage(EnsureLowerHalfAddr(pt));
    }

    void PageTableManager::Teardown(bool includeHigherHalf)
    {
        FreePageTable(EnsureHigherHalfAddr(topLevelAddress.As<PageTable>()), pagingLevels, includeHigherHalf);
    }

    sl::Opt<sl::NativePtr> PageTableManager::GetPhysicalAddress(sl::NativePtr virtAddr)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        
        PageTable* pageTable = EnsureHigherHalfAddr(topLevelAddress.As<PageTable>());
        PageTableEntry* entry;

        size_t tableIndices[6];
        GetPageTableIndices(virtAddr, tableIndices);

        for (size_t level = pagingLevels; level > 0; level--)
        {
            entry = &pageTable->entries[tableIndices[level]];
            
            if (!entry->HasFlag(PageEntryFlag::Present))
                return {};
            if (level > 1 && entry->HasFlag(PageEntryFlag::PageSize))
            {
                //trim the parts of the address we've translated, keep the bits that are used as the offset
                if (level == 2)
                    virtAddr.raw &= 0x1FFFFF;
                else if (level == 3)
                    virtAddr.raw &= 0x3FFFFFFF;
                break;
            }
            if (level == 1)
                virtAddr.raw &= 0xFFF;

            pageTable = EnsureHigherHalfAddr(entry->GetAddr().As<PageTable>());
        }

        return sl::NativePtr(entry->GetAddr().raw + virtAddr.raw);
    }

    void PageTableManager::MakeActive() const
    {
        WriteCR3(topLevelAddress.raw);
    }

    bool PageTableManager::IsActive() const
    {
        return ReadCR3() == topLevelAddress.raw;
    }

    bool PageTableManager::PageSizeAvailable(PagingSize size) const
    {
        //this is pretty ugly I know, but it allows us to have 'Physical' be the same as one of the pre-determined sizes.
        switch (size)
        {
        case PagingSize::Physical:
            return true; //always available
#if PAGE_FRAME_SIZE != 0x1000
        case PagingSize::_4KB:
            return true;
#endif
#if PAGE_FRAME_SIZE != 0x200000
        case PagingSize::_2MB:
            return true;
#endif
#if PAGE_FRAME_SIZE != 0x40000000
        case PagingSize::_1GB:
            return CPU::FeatureSupported(CpuFeature::GigabytePages);
#endif

        default:
            return false;
        }
    }

    sl::NativePtr PageTableManager::GetTla() const
    { return topLevelAddress; }

    void PageTableManager::MapMemory(sl::NativePtr virtAddr, MemoryMapFlags flags)
    {
        sl::NativePtr physAddr = PMM::Global()->AllocPage();
        if (physAddr.ptr == nullptr)
            Log("Could not allocate enough physical memory to map!", LogSeverity::Fatal);

        MapMemory(virtAddr, physAddr, PagingSize::Physical, flags);
    }

    void PageTableManager::MapMemory(sl::NativePtr virtAddr, sl::NativePtr physAddr, MemoryMapFlags flags)
    {
        MapMemory(virtAddr, physAddr, PagingSize::Physical, flags);
    }

    void PageTableManager::MapMemory(sl::NativePtr virtAddr, PagingSize pageSize, MemoryMapFlags flags)
    {
        if (!PageSizeAvailable(pageSize))
        {
            Log("Cannot map memory, that paging size is not supported on this system.", LogSeverity::Error);
            return;
        }

        const size_t nativePagesRequired = (size_t)pageSize / (size_t)PagingSize::Physical;
        sl::NativePtr physAddr = PMM::Global()->AllocPages(nativePagesRequired);
        if (physAddr.ptr == nullptr)
            Log("Could not allocate enough physical memory to map!", LogSeverity::Fatal);
        
        MapMemory(virtAddr, physAddr, pageSize, flags);
    }

    void PageTableManager::MapMemory(sl::NativePtr virtAddr, sl::NativePtr physAddr, PagingSize size, MemoryMapFlags flags)
    {
        if (!PageSizeAvailable(size))
        {
            Log("Cannot map memory, that paging size is not supported on this system.", LogSeverity::Error);
            return;
        }

        sl::ScopedSpinlock scopeLock(&lock);

        //these are applied to all new entries
        PageEntryFlag templateFlags = PageEntryFlag::Present | PageEntryFlag::RegionWritesAllowed;
        if (sl::EnumHasFlag(flags, MemoryMapFlags::UserAccessible))
            templateFlags = sl::EnumSetFlag(templateFlags, PageEntryFlag::UserAccessAllowed);

        //applied to the final entry
        PageEntryFlag finalFlags = PageEntryFlag::Present | GetPageEntryFlags(flags);
        if (size == PagingSize::_2MB || size == PagingSize::_1GB)
            finalFlags = finalFlags | PageEntryFlag::PageSize;

        size_t tableIndices[6];
        GetPageTableIndices(virtAddr, tableIndices);

        PageTable* pageTable = EnsureHigherHalfAddr(topLevelAddress.As<PageTable>());
        PageTableEntry* entry;
        for (size_t level = pagingLevels; level > 0; level--)
        {
            entry = &pageTable->entries[tableIndices[level]];
            
            if (size == PagingSize::_1GB && level == 3)
                break;
            if (size == PagingSize::_2MB && level == 2)
                break;
            if (level == 1)
                break;

            if (!entry->HasFlag(PageEntryFlag::Present))
            {
                entry->SetAddr(PMM::Global()->AllocPage());
                entry->SetFlag(templateFlags);
                sl::memset(EnsureHigherHalfAddr(entry->GetAddr().ptr), 0, PAGE_FRAME_SIZE);
            }
            pageTable = EnsureHigherHalfAddr(entry->GetAddr().As<PageTable>());
        }

        entry->SetFlag(finalFlags);
        entry->SetAddr(physAddr);
        InvalidatePage(virtAddr);
    }

    void PageTableManager::MapRange(sl::NativePtr virtAddrBase, size_t count, MemoryMapFlags flags)
    {
        for (size_t i = 0; i < count; i++)
            MapMemory(virtAddrBase.raw + (i * PAGE_FRAME_SIZE), flags);
    }

    void PageTableManager::MapRange(sl::NativePtr virtAddrBase, sl::NativePtr physAddrBase, size_t count, MemoryMapFlags flags)
    {
        for (size_t i = 0; i < count; i++)
            MapMemory(virtAddrBase.raw + (i * PAGE_FRAME_SIZE), physAddrBase.raw + (i * PAGE_FRAME_SIZE), flags);
    }

    void PageTableManager::MapRange(sl::NativePtr virtAddrBase, size_t count, PagingSize pageSize, MemoryMapFlags flags)
    {
        for (size_t i = 0; i < count; i++)
            MapMemory(virtAddrBase.raw + (i * PAGE_FRAME_SIZE), pageSize, flags);
    }

    void PageTableManager::MapRange(sl::NativePtr virtAddrBase, sl::NativePtr physAddrBase, size_t count, PagingSize pageSize, MemoryMapFlags flags)
    {
        for (size_t i = 0; i < count; i++)
            MapMemory(virtAddrBase.raw + (i * PAGE_FRAME_SIZE), physAddrBase.raw + (i * PAGE_FRAME_SIZE), pageSize, flags);
    }

    PagingSize PageTableManager::UnmapMemory(sl::NativePtr virtAddr, bool freePhysicalPage)
    {
        sl::ScopedSpinlock scopeLock(&lock);

        size_t tableIndices[6];
        GetPageTableIndices(virtAddr, tableIndices);

        PageTable* pageTable = EnsureHigherHalfAddr(topLevelAddress.As<PageTable>());
        PageTableEntry* entry;
        PagingSize freedSize = PagingSize::Physical;
        for (size_t level = pagingLevels; level > 0; level--)
        {
            entry = &pageTable->entries[tableIndices[level]];
            if (!entry->HasFlag(PageEntryFlag::Present))
                return PagingSize::NoSize; //not even mapped, no page size
            
            if (level > 1 && entry->HasFlag(PageEntryFlag::PageSize))
            {
                if (level == 3)
                    freedSize = PagingSize::_1GB;
                else if (level == 2)
                    freedSize = PagingSize::_2MB;
                break;
            }

            pageTable = EnsureHigherHalfAddr(entry->GetAddr().As<PageTable>());
        }

        if (freePhysicalPage)
            PMM::Global()->FreePages(entry->GetAddr().ptr, (size_t)freedSize / (size_t)PagingSize::Physical);
        entry->raw = 0ul;
        InvalidatePage(virtAddr);
        return freedSize;
    }

    PagingSize PageTableManager::UnmapRange(sl::NativePtr virtAddrBase, size_t count, bool freePhysicalPages)
    {
        for (size_t i = 0; i < count; i++)
            UnmapMemory(virtAddrBase.raw + (i * PAGE_FRAME_SIZE), freePhysicalPages);

        return PagingSize::Physical;
    }
}
