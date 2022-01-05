#include <memory/PhysicalMemory.h>
#include <memory/Paging.h>
#include <Memory.h>
#include <boot/Stivale2.h>
#include <Utilities.h>
#include <Platform.h>
#include <Cpu.h>
#include <Log.h>
#include <Maths.h>

namespace Kernel
{
    //defined in KernelMain.cpp.
    extern stivale2_tag* FindStivaleTagInternal(uint64_t id);
    template<typename TagType>
    TagType FindStivaleTag(uint64_t id)
    { return reinterpret_cast<TagType>(FindStivaleTagInternal(id)); }
}

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
            raw |= flagMask & 0xFFF0'0000'0000'0FFF;
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

    PageEntryFlag GetPageEntryFlags(MemoryMapFlag flags)
    {
        PageEntryFlag finalFlags = PageEntryFlag::None;

        if (sl::EnumHasFlag(flags, MemoryMapFlag::AllowWrites))
            finalFlags = finalFlags | PageEntryFlag::RegionWritesAllowed;
        if (sl::EnumHasFlag(flags, MemoryMapFlag::UserAccessible))
            finalFlags = finalFlags | PageEntryFlag::UserAccessAllowed;
        if (CPU::FeatureSupported(CpuFeature::ExecuteDisable) && sl::EnumHasFlag(flags, MemoryMapFlag::AllowExecute))
            finalFlags = finalFlags | PageEntryFlag::ExecuteDisable;

        return finalFlags;
    }

    FORCE_INLINE void GetPageMapIndices(sl::NativePtr virtAddr, uint64_t* pml5Index, uint64_t* pml4Index, uint64_t* pml3Index, uint64_t* pml2Index, uint64_t* pml1Index)
    {
        //this works for all levels of paging, since the unused address bits are required to be zero (and ignored).
        //This of course breaks with 32bit paging (it may work with PAE paging, not tested and not interested).
        *pml5Index = (virtAddr.raw >> 48) & 0x1FF;
        *pml4Index = (virtAddr.raw >> 39) & 0x1FF;
        *pml3Index = (virtAddr.raw >> 30) & 0x1FF;
        *pml2Index = (virtAddr.raw >> 21) & 0x1FF;
        *pml1Index = (virtAddr.raw >> 12) & 0x1FF;
    }

    FORCE_INLINE
    void PageTableManager::InvalidatePage(sl::NativePtr virtualAddress) const
    {
        asm volatile("invlpg %0" :: "m"(virtualAddress.ptr));
    }

    PageTableManager defaultPageTableManager;
    PageTableManager* PageTableManager::Local()
    {
        return &defaultPageTableManager;
    }

    bool PageTableManager::usingExtendedPaging;
    
    void PageTableManager::Setup()
    {
        Log("Beginning system paging setup for pre-scheduler kernel.", LogSeverity::Info);

        if (CPU::FeatureSupported(CpuFeature::ExecuteDisable))
        {
            //ensure EFER.NX is enabled (might already be enabled from bootloader)
            uint64_t eferCurrent = CPU::ReadMsr(MSR_IA32_EFER);
            eferCurrent |= (1 << 11);
            CPU::WriteMsr(MSR_IA32_EFER, eferCurrent);

            Log("Execute disable is available, and enabled in EFER.", LogSeverity::Verbose);
        }
        else
            Log("Execute disable not available.", LogSeverity::Verbose);

        if (CPU::FeatureSupported(CpuFeature::GigabytePages))
            Log("Gigabyte pages are available.", LogSeverity::Verbose);

        uint64_t cr4 = ReadCR4();
        if ((cr4 & (1 << 12)) != 0)
        {
            usingExtendedPaging = true;
            Log("System is setup for 5-level paging.", LogSeverity::Verbose);
        }
        else
        {
            usingExtendedPaging = false;
            Log("System is setup for 4-level paging.", LogSeverity::Verbose);
        }

        Log("Paging setup successful.", LogSeverity::Info);
    }
    
    void PageTableManager::InitKernel(bool reuseBootloaderMaps)
    {
        if (reuseBootloaderMaps)
            topLevelAddress = ReadCR3();
        else
        {
            topLevelAddress = PMM::Global()->AllocPage();
            sl::memset(topLevelAddress.ptr, 0, sizeof(PageTable));

            stivale2_struct_tag_hhdm* hhdmTag = FindStivaleTag<stivale2_struct_tag_hhdm*>(STIVALE2_STRUCT_TAG_HHDM_ID);
            stivale2_struct_tag_pmrs* pmrsTag = FindStivaleTag<stivale2_struct_tag_pmrs*>(STIVALE2_STRUCT_TAG_PMRS_ID);
            stivale2_struct_tag_kernel_base_address* baseAddrTag = FindStivaleTag<stivale2_struct_tag_kernel_base_address*>(STIVALE2_STRUCT_TAG_KERNEL_BASE_ADDRESS_ID);
            stivale2_struct_tag_memmap* mmapTag = FindStivaleTag<stivale2_struct_tag_memmap*>(STIVALE2_STRUCT_TAG_MEMMAP_ID);

            //map kernel binary using pmrs + kernel base
            for (size_t i = 0; i < pmrsTag->entries; i++)
            {
                const stivale2_pmr* pmr = &pmrsTag->pmrs[i];
                const size_t pagesRequired = pmr->length / PAGE_FRAME_SIZE + 1;
                const uint64_t physBase = baseAddrTag->physical_base_address + (pmr->base - baseAddrTag->virtual_base_address);

                MemoryMapFlag flags = MemoryMapFlag::None;
                if ((pmr->permissions & STIVALE2_PMR_EXECUTABLE) != 0)
                    flags = sl::EnumSetFlag(flags, MemoryMapFlag::AllowExecute);
                if ((pmr->permissions & STIVALE2_PMR_WRITABLE) != 0)
                    flags = sl::EnumSetFlag(flags, MemoryMapFlag::AllowWrites);

                for (size_t j = 0; j < pagesRequired; j++)
                    MapMemory(pmr->base + (j * PAGE_FRAME_SIZE), physBase + (j * PAGE_FRAME_SIZE), flags);
            }

            //nmap the first 4gb of physical memory at a known address (we're reusing the address specified by hddm tag for now)
            constexpr size_t memUpper = 1 * GB; //TODO: investigate why 4gb here causes slowdowns - bigger pages perhaps?
            for (size_t i = 0; i < memUpper; i += PAGE_FRAME_SIZE)
                MapMemory(hhdmTag->addr + i, i, MemoryMapFlag::AllowWrites);
            
            //map any regions of memory that are above the 4gb mark
            for (size_t i = 0; i < mmapTag->entries; i++)
            {
                const stivale2_mmap_entry* region = &mmapTag->memmap[i];

                if (region->base + region->length < memUpper)
                    continue; //will have already been identity mapped as part of previous step
                if (region->type == STIVALE2_MMAP_BAD_MEMORY)
                    continue; //map everything that isnt just bad memory
                
                size_t base = region->base;
                if (base < memUpper)
                    base = memUpper;

                for (size_t j = 0; j < region->length; j += PAGE_FRAME_SIZE)
                    MapMemory(hhdmTag->addr + base + j, base + j, MemoryMapFlag::AllowWrites);
            }

            MakeActive();
        }

        Log("Kernel root page table initialized.", LogSeverity::Info);
    }

    void PageTableManager::InitClone()
    {
        ScopedSpinlock scopeLock(&lock);
        
        Log("Freshly cloned page table initialized.", LogSeverity::Info);
    }

    bool PageTableManager::EnsurePageFlags(sl::NativePtr virtAddr, MemoryMapFlag flags, bool overwriteExisting)
    {
        PageTable* pageTable = EnsureHigherHalfAddr(topLevelAddress.As<PageTable>());
        PageTableEntry* entry;

        uint64_t pml5Index, pml4Index, pml3Index, pml2Index, pml1Index;
        GetPageMapIndices(virtAddr, &pml5Index, &pml4Index, &pml3Index, &pml2Index, &pml1Index);

        PageEntryFlag entryFlags = GetPageEntryFlags(flags);

        if (usingExtendedPaging)
        {
            entry = &pageTable->entries[pml5Index];
            if (!entry->HasFlag(PageEntryFlag::Present))
                return false;
            if (overwriteExisting)
                entry->ClearFlag(static_cast<PageEntryFlag>((uint64_t)-1));
            entry->SetFlag(entryFlags);
            pageTable = EnsureHigherHalfAddr(entry->GetAddr().As<PageTable>());
        }

        entry = &pageTable->entries[pml4Index];
        if (!entry->HasFlag(PageEntryFlag::Present))
            return false;
        if (overwriteExisting)
            entry->ClearFlag(static_cast<PageEntryFlag>((uint64_t)-1));
        entry->SetFlag(entryFlags);
        pageTable = EnsureHigherHalfAddr(entry->GetAddr().As<PageTable>());

        entry = &pageTable->entries[pml3Index];
        if (!entry->HasFlag(PageEntryFlag::Present))
            return false;
        if (overwriteExisting)
            entry->ClearFlag(static_cast<PageEntryFlag>((uint64_t)-1));
        entry->SetFlag(entryFlags);
        pageTable = EnsureHigherHalfAddr(entry->GetAddr().As<PageTable>());
         if (entry->HasFlag(PageEntryFlag::PageSize))
            return true; //1gb page

        entry = &pageTable->entries[pml2Index];
        if (!entry->HasFlag(PageEntryFlag::Present))
            return false;
        if (overwriteExisting)
            entry->ClearFlag(static_cast<PageEntryFlag>((uint64_t)-1));
        entry->SetFlag(entryFlags);
        pageTable = EnsureHigherHalfAddr(entry->GetAddr().As<PageTable>());
        if (entry->HasFlag(PageEntryFlag::PageSize))
            return true; //2mb page

        entry = &pageTable->entries[pml1Index];
        if (!entry->HasFlag(PageEntryFlag::Present))
            return false;
        if (overwriteExisting)
            entry->ClearFlag(static_cast<PageEntryFlag>((uint64_t)-1));
        entry->SetFlag(entryFlags);

        return true;
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

    void PageTableManager::MapMemory(sl::NativePtr virtAddr, MemoryMapFlag flags)
    {
        sl::NativePtr physAddr = PMM::Global()->AllocPage();
        if (physAddr.ptr == nullptr)
            Log("Could not allocate enough physical memory to map!", LogSeverity::Fatal);

        MapMemory(virtAddr, physAddr, PagingSize::Physical, flags);
    }

    void PageTableManager::MapMemory(sl::NativePtr virtAddr, sl::NativePtr physAddr, MemoryMapFlag flags)
    {
        MapMemory(virtAddr, physAddr, PagingSize::Physical, flags);
    }

    void PageTableManager::MapMemory(sl::NativePtr virtAddr, PagingSize pageSize, MemoryMapFlag flags)
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
        
        //emit an error and then trash the tlb cache in protest.
        Log("Non-4K paging is not supported yet.", LogSeverity::Error);
    }

    void PageTableManager::MapMemory(sl::NativePtr virtAddr, sl::NativePtr physAddr, PagingSize size, MemoryMapFlag flags)
    {
        if (size == PagingSize::NoSize)
        {
            Log("Tried to map memory with no page size, ignoring.", LogSeverity::Warning);
            return;
        }

        ScopedSpinlock scopeLock(&lock);

        //these are applied to all new entries
        PageEntryFlag templateFlags = PageEntryFlag::Present | PageEntryFlag::RegionWritesAllowed;
        //applied to the final entry
        PageEntryFlag finalFlags = PageEntryFlag::Present | GetPageEntryFlags(flags);

        uint64_t pml5Index, pml4Index, pml3Index, pml2Index, pml1Index;
        GetPageMapIndices(virtAddr, &pml5Index, &pml4Index, &pml3Index, &pml2Index, &pml1Index);

        PageTable* pageTable = EnsureHigherHalfAddr(topLevelAddress.As<PageTable>());
        PageTableEntry* entry;
        if (usingExtendedPaging)
        {
            //handle pml5 table
            entry = &pageTable->entries[pml5Index];
            
            if (!entry->HasFlag(PageEntryFlag::Present))
            {
                entry->SetAddr(PMM::Global()->AllocPage());
                sl::memset(EnsureHigherHalfAddr(entry->GetAddr().ptr), 0, PAGE_FRAME_SIZE);
                entry->SetFlag(templateFlags);
            }

            pageTable = EnsureHigherHalfAddr(entry->GetAddr().As<PageTable>());
        }

        //handle pml4
        entry = &pageTable->entries[pml4Index];
        if (!entry->HasFlag(PageEntryFlag::Present))
        {
            entry->SetAddr(PMM::Global()->AllocPage());
            sl::memset(EnsureHigherHalfAddr(entry->GetAddr().ptr), 0, PAGE_FRAME_SIZE);
            entry->SetFlag(templateFlags);
        }
        pageTable = EnsureHigherHalfAddr(entry->GetAddr().As<PageTable>());

        //for 1GB pages - early exit
        entry = &pageTable->entries[pml3Index];
        if (size == PagingSize::_1GB && CPU::FeatureSupported(CpuFeature::GigabytePages))
        {
            entry->SetFlag(finalFlags);
            entry->SetFlag(PageEntryFlag::PageSize);
            entry->SetAddr(physAddr);
            return;
        }

        //handle pml3
        if (!entry->HasFlag(PageEntryFlag::Present))
        {
            entry->SetAddr(PMM::Global()->AllocPage());
            sl::memset(EnsureHigherHalfAddr(entry->GetAddr().ptr), 0, PAGE_FRAME_SIZE);
            entry->SetFlag(templateFlags);
        }
        pageTable = EnsureHigherHalfAddr(entry->GetAddr().As<PageTable>());

        //for 2MB pages - early exit
        entry = &pageTable->entries[pml2Index];
        if (size == PagingSize::_2MB)
        {
            entry->SetFlag(finalFlags);
            entry->SetFlag(PageEntryFlag::PageSize); //tell cpu to end it's search here
            entry->SetAddr(physAddr);
            return;
        }

        //handle pml2
        if (!entry->HasFlag(PageEntryFlag::Present))
        {
            entry->SetAddr(PMM::Global()->AllocPage());
            sl::memset(EnsureHigherHalfAddr(entry->GetAddr().ptr), 0, PAGE_FRAME_SIZE);
            entry->SetFlag(templateFlags);
        }
        pageTable = EnsureHigherHalfAddr(entry->GetAddr().As<PageTable>());

        //handle pml1
        entry = &pageTable->entries[pml1Index];
        entry->SetFlag(finalFlags);
        entry->SetAddr(physAddr);

        InvalidatePage(physAddr);
    }

    void PageTableManager::MapRange(sl::NativePtr virtAddrBase, size_t count, MemoryMapFlag flags)
    {
        for (size_t i = 0; i < count; i++)
            MapMemory(virtAddrBase.raw + (i * PAGE_FRAME_SIZE), flags);
    }

    void PageTableManager::MapRange(sl::NativePtr virtAddrBase, sl::NativePtr physAddrBase, size_t count, MemoryMapFlag flags)
    {
        for (size_t i = 0; i < count; i++)
            MapMemory(virtAddrBase.raw + (i * PAGE_FRAME_SIZE), physAddrBase.raw + (i * PAGE_FRAME_SIZE), flags);
    }

    void PageTableManager::MapRange(sl::NativePtr virtAddrBase, size_t count, PagingSize pageSize, MemoryMapFlag flags)
    {
        for (size_t i = 0; i < count; i++)
            MapMemory(virtAddrBase.raw + (i * PAGE_FRAME_SIZE), pageSize, flags);
    }

    void PageTableManager::MapRange(sl::NativePtr virtAddrBase, sl::NativePtr physAddrBase, size_t count, PagingSize pageSize, MemoryMapFlag flags)
    {
        for (size_t i = 0; i < count; i++)
            MapMemory(virtAddrBase.raw + (i * PAGE_FRAME_SIZE), physAddrBase.raw + (i * PAGE_FRAME_SIZE), pageSize, flags);
    }

    PagingSize PageTableManager::UnmapMemory(sl::NativePtr virtAddr)
    {
        uint64_t pml5Index, pml4Index, pml3Index, pml2Index, pml1Index;
        GetPageMapIndices(virtAddr, &pml5Index, &pml4Index, &pml3Index, &pml2Index, &pml1Index);

        ScopedSpinlock scopeLock(&lock);

        PageTable* pageTable = EnsureHigherHalfAddr(topLevelAddress.As<PageTable>());
        PageTableEntry* entry;
        if (usingExtendedPaging)
        {
            //pml5
            entry = &pageTable->entries[pml5Index];
            if (!entry->HasFlag(PageEntryFlag::Present))
                return PagingSize::NoSize; //page not accessible, about
            pageTable = EnsureHigherHalfAddr(entry->GetAddr().As<PageTable>());
        }

        //pml4
        entry = &pageTable->entries[pml4Index];
        if (!entry->HasFlag(PageEntryFlag::Present))
            return PagingSize::NoSize;
        pageTable = EnsureHigherHalfAddr(entry->GetAddr().As<PageTable>());

        //pml3 and 1gb pages
        entry = &pageTable->entries[pml3Index];
        if (!entry->HasFlag(PageEntryFlag::Present))
            return PagingSize::NoSize;
        if (entry->HasFlag(PageEntryFlag::PageSize))
        {
            //it's a 1gb page
            PMM::Global()->FreePages(entry->GetAddr().ptr, (uint64_t)PagingSize::_1GB / (uint64_t)PagingSize::Physical);
            entry->SetAddr(0ul);
            entry->ClearFlag(~PageEntryFlag::None);
            return PagingSize::_1GB;
        }
        else
            pageTable = EnsureHigherHalfAddr(entry->GetAddr().As<PageTable>());

        //pml2 and 2mb pages
        entry = &pageTable->entries[pml3Index];
        if (!entry->HasFlag(PageEntryFlag::Present))
            return PagingSize::NoSize;
        if (entry->HasFlag(PageEntryFlag::PageSize))
        {
            PMM::Global()->FreePages(entry->GetAddr().ptr, (uint64_t)PagingSize::_2MB / (uint64_t)PagingSize::Physical);
            entry->SetAddr(0ul);
            entry->ClearFlag(~PageEntryFlag::None);
            return PagingSize::_2MB;
        }
        else
            pageTable = EnsureHigherHalfAddr(entry->GetAddr().As<PageTable>());

        //pml1
        entry = &pageTable->entries[pml1Index];
        if (!entry->HasFlag(PageEntryFlag::Present))
            return PagingSize::NoSize;
        PMM::Global()->FreePage(entry->GetAddr().ptr);
        entry->SetAddr(0ul);
        entry->ClearFlag(~PageEntryFlag::None);
        
        //unmap memory
        InvalidatePage(virtAddr);
        return PagingSize::Physical;
    }

    PagingSize PageTableManager::UnmapRange(sl::NativePtr virtAddrBase, size_t count)
    {
        for (size_t i = 0; i < count; i++)
            UnmapMemory(virtAddrBase.raw + (i * PAGE_FRAME_SIZE));

        return PagingSize::Physical;
    }
}
