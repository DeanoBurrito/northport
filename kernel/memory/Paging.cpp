#include <memory/PhysicalMemory.h>
#include <memory/Paging.h>
#include <Memory.h>
#include <Utilities.h>
#include <Platform.h>
#include <Cpu.h>
#include <Log.h>

namespace Kernel::Memory
{   
    /*  Notes about current implementation:
            - Supports 4 and 5 level paging
            - DOES NOT map pages it allocates for page tables
            - Page table entries use generic 'writes enabled, present' flags. Requested flags only apply to the final entry.
        
        Improvements to be made:
            - MapRange() will be super useful
            - Get rid of reusing bootloader page maps, figure out what we're missing with our own map
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

    void PageTableManager::Init(bool reuseBootloaderMaps)
    {
        if (reuseBootloaderMaps)
            topLevelAddress = ReadCR3();
        else
        {
            topLevelAddress = PMM::Global()->AllocPage();
            sl::memset(topLevelAddress.ptr, 0, sizeof(PageTable));
        }

        Log("New page table initialized", LogSeverity::Info);
    }

    void PageTableManager::SetKernelPageRefs(sl::NativePtr tla)
    {   
        SpinlockAcquire(&lock);
        
        PageTable* foreignTable = tla.As<PageTable>();
        PageTable* localTable = topLevelAddress.As<PageTable>();
        for (size_t i = 0x100; i < 0x200; i++)
        {
            localTable->entries[i] = foreignTable->entries[i];
        }

        SpinlockRelease(&lock);
    }

    bool PageTableManager::EnsurePageFlags(sl::NativePtr virtAddr, MemoryMapFlag flags, bool overwriteExisting)
    {
        PageTable* pageTable = topLevelAddress.As<PageTable>();
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
            pageTable = entry->GetAddr().As<PageTable>();
        }

        entry = &pageTable->entries[pml4Index];
        if (!entry->HasFlag(PageEntryFlag::Present))
            return false;
        if (overwriteExisting)
            entry->ClearFlag(static_cast<PageEntryFlag>((uint64_t)-1));
        entry->SetFlag(entryFlags);
        pageTable = entry->GetAddr().As<PageTable>();

        entry = &pageTable->entries[pml3Index];
        if (!entry->HasFlag(PageEntryFlag::Present))
            return false;
        if (overwriteExisting)
            entry->ClearFlag(static_cast<PageEntryFlag>((uint64_t)-1));
        entry->SetFlag(entryFlags);
        pageTable = entry->GetAddr().As<PageTable>();
         if (entry->HasFlag(PageEntryFlag::PageSize))
            return true; //1gb page

        entry = &pageTable->entries[pml2Index];
        if (!entry->HasFlag(PageEntryFlag::Present))
            return false;
        if (overwriteExisting)
            entry->ClearFlag(static_cast<PageEntryFlag>((uint64_t)-1));
        entry->SetFlag(entryFlags);
        pageTable = entry->GetAddr().As<PageTable>();
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
        InvalidatePage(virtAddr);
    }

    void PageTableManager::MapMemory(sl::NativePtr virtAddr, sl::NativePtr physAddr, MemoryMapFlag flags)
    {
        MapMemory(virtAddr, physAddr, PagingSize::Physical, flags);
        InvalidatePage(virtAddr);
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
        InvalidatePage(virtAddr);
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

        PageTable* pageTable = topLevelAddress.As<PageTable>();
        PageTableEntry* entry;
        if (usingExtendedPaging)
        {
            //handle pml5 table
            entry = &pageTable->entries[pml5Index];
            
            if (!entry->HasFlag(PageEntryFlag::Present))
            {
                entry->SetAddr(PMM::Global()->AllocPage());
                sl::memset(entry->GetAddr().ptr, 0, PAGE_FRAME_SIZE);
                entry->SetFlag(templateFlags);
            }

            pageTable = entry->GetAddr().As<PageTable>();
        }

        //handle pml4
        entry = &pageTable->entries[pml4Index];
        if (!entry->HasFlag(PageEntryFlag::Present))
        {
            entry->SetAddr(PMM::Global()->AllocPage());
            sl::memset(entry->GetAddr().ptr, 0, PAGE_FRAME_SIZE);
            entry->SetFlag(templateFlags);
        }
        pageTable = entry->GetAddr().As<PageTable>();

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
            sl::memset(entry->GetAddr().ptr, 0, PAGE_FRAME_SIZE);
            entry->SetFlag(templateFlags);
        }
        pageTable = entry->GetAddr().As<PageTable>();

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
            sl::memset(entry->GetAddr().ptr, 0, PAGE_FRAME_SIZE);
            entry->SetFlag(templateFlags);
        }
        pageTable = entry->GetAddr().As<PageTable>();

        //handle pml1
        entry = &pageTable->entries[pml1Index];
        entry->SetFlag(finalFlags);
        entry->SetAddr(physAddr);

        InvalidatePage(physAddr);
    }

    PagingSize PageTableManager::UnmapMemory(sl::NativePtr virtAddr)
    {
        uint64_t pml5Index, pml4Index, pml3Index, pml2Index, pml1Index;
        GetPageMapIndices(virtAddr, &pml5Index, &pml4Index, &pml3Index, &pml2Index, &pml1Index);

        ScopedSpinlock scopeLock(&lock);

        PageTable* pageTable = topLevelAddress.As<PageTable>();
        PageTableEntry* entry;
        if (usingExtendedPaging)
        {
            //pml5
            entry = &pageTable->entries[pml5Index];
            if (!entry->HasFlag(PageEntryFlag::Present))
                return PagingSize::NoSize; //page not accessible, about
            pageTable = entry->GetAddr().As<PageTable>();
        }

        //pml4
        entry = &pageTable->entries[pml4Index];
        if (!entry->HasFlag(PageEntryFlag::Present))
            return PagingSize::NoSize;
        pageTable = entry->GetAddr().As<PageTable>();

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
            pageTable = entry->GetAddr().As<PageTable>();

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
            pageTable = entry->GetAddr().As<PageTable>();

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
}
