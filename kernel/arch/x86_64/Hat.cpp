#include <arch/Hat.h>
#include <arch/Cpu.h>
#include <memory/Pmm.h>
#include <debug/Log.h>
#include <Memory.h>
#include <Maths.h>

namespace Npk
{
    enum class PageSizes : size_t
    {
        _4K = 1,
        _2M = 2,
        _1G = 3,
    };

    constexpr size_t PageTableEntries = 512;

    struct PageTable
    {
        uint64_t entries[PageTableEntries];
    };

    struct HatMap
    {
        PageTable* root;
        sl::Atomic<uint32_t> generation;
    };

    constexpr inline size_t GetPageSize(PageSizes size)
    { return 1ul << (12 + 9 * ((size_t)size - 1)); }

    constexpr uint64_t PresentFlag = 1 << 0;

    size_t pagingLevels;
    uintptr_t addrMask;
    bool nxSupported;
    bool globalPagesSupported;

    HatMap kernelMap;

    HatLimits limits 
    {
        .modeCount = 3,
        .modes = 
        {
            { .granularity = GetPageSize(PageSizes::_4K) },
            { .granularity = GetPageSize(PageSizes::_2M) },
            { .granularity = GetPageSize(PageSizes::_1G) },
        }
    };

    void HatInit()
    {
        //Note that we only read the paging mode, the system should have set the appropriate
        //mode long before getting to this stage. This is just querying what was established.
        const uint64_t cr4 = ReadCr4();
        pagingLevels = (cr4 & (1 << 12)) ? 5 : 4; //bit 12 is LA57 (5-level paging).

        const size_t maxTranslationLevel = CpuHasFeature(CpuFeature::Pml3Translation) ? 3 : 2;
        if (!CpuHasFeature(CpuFeature::Pml3Translation))
            limits.modeCount = 2; //if the cpu doesn't support gigabyte pages, don't advertise it.
        nxSupported = CpuHasFeature(CpuFeature::NoExecute);
        globalPagesSupported = CpuHasFeature(CpuFeature::GlobalPages);

        //determine the mask needed to separate the physical address from the flags
        addrMask = 1ul << (9 * pagingLevels + 12);
        addrMask--;
        addrMask &= ~0xFFFul;

        //create the master set of page tables used for the kernel, stash the details in `kernelMap`.
        kernelMap.generation = 0;
        kernelMap.root = reinterpret_cast<PageTable*>(PMM::Global().Alloc());
        sl::memset(AddHhdm(kernelMap.root), 0, PageSize);

        //mapping the HHDM would be more appropriate for the kernel VM driver to do, but
        //we can optimize quite a bit by doing it in the HAT layer.
        //Start with some sanity checks, then determining what page size to use.
        ASSERT(hhdmLength >= limits.modes[0].granularity, "Why is the HHDM < 4K?");
        size_t hhdmPageSize = maxTranslationLevel;
        while (GetPageSize((PageSizes)hhdmPageSize) > hhdmLength)
            hhdmPageSize--;
        
        //now map the hhdm
        constexpr HatFlags hhdmFlags = HatFlags::Write | HatFlags::Global;
        for (uintptr_t i = 0; i < hhdmLength; i += GetPageSize((PageSizes)hhdmPageSize))
            Map(KernelMap(), hhdmBase + i, i, hhdmPageSize - 1, hhdmFlags, false);
        
        //adjust HHDM length so matches the memory we mapped, since the VMM uses hhdmLength
        //to know where it can start allocating virtual address space.
        hhdmLength = sl::AlignUp(hhdmLength, GetPageSize((PageSizes)hhdmPageSize));

        constexpr const char* SizeStrs[] = { "", "4KiB", "2MiB", "1GiB" };
        Log("HHDM mapped with %s pages", LogLevel::Verbose, SizeStrs[hhdmPageSize]);
        Log("Hat init (paging): levels=%lu, maxMapSize=%s, nx=%s, globalPages=%s", LogLevel::Info,
            pagingLevels, SizeStrs[maxTranslationLevel],
            nxSupported ? "yes" : "no", globalPagesSupported ? "yes" : "no");
    }

    const HatLimits& GetHatLimits()
    { return limits; }

    HatMap* InitNewMap()
    {
        HatMap* map = new HatMap;
        map->root = reinterpret_cast<PageTable*>(PMM::Global().Alloc());
        sl::memset(AddHhdm(map->root), 0, PageSize / 2);

        SyncWithMasterMap(map);
        return map;
    }

    void CleanupPageTable(PageTable* pt)
    {
        if (pt == nullptr)
            return;

        for (size_t i = 0; i < PageTableEntries; i++)
        {
            if ((pt->entries[i] & PresentFlag) == 0)
                continue;

            const uint64_t entry = pt->entries[i];
            CleanupPageTable(reinterpret_cast<PageTable*>((entry & addrMask) + hhdmBase));
        }

        PMM::Global().Free(reinterpret_cast<uintptr_t>(pt) - hhdmBase, 1);
    }

    void CleanupMap(HatMap* map)
    {
        if (map == nullptr)
            return;

        CleanupPageTable(AddHhdm(map->root));
        delete map;
    }

    HatMap* KernelMap()
    { return &kernelMap; }

    inline void GetAddressIndices(uintptr_t vaddr, size_t* indices)
    {
        if (pagingLevels > 4)
            indices[5] = (vaddr >> 48) & 0x1FF;
        indices[4] = (vaddr >> 39) & 0x1FF;
        indices[3] = (vaddr >> 30) & 0x1FF;
        indices[2] = (vaddr >> 21) & 0x1FF;
        indices[1] = (vaddr >> 12) & 0x1FF;
    }
    
    bool Map(HatMap* map, uintptr_t vaddr, uintptr_t paddr, size_t mode, HatFlags flags, bool flush)
    {
        ASSERT(map != nullptr, "HatMap is nullptr");
        if (mode >= limits.modeCount)
            return false; //invalid mode
        
        size_t indices[pagingLevels + 1];
        GetAddressIndices(vaddr, indices);

        /*
            I've chosen to use a loop-based approach to walking the page tables, you could also
            do this recursively too. I find this to be simpler and the recursive approach can eat
            away at stack space (if TCO fails).
            It should be noted that the current page level is 1-based, not 0-based:
            meaning the pml4 is level 4, and the last level of the page tables is level 1, there is
            level 0.
        */
        uint64_t* entry = nullptr;
        PageTable* pt = AddHhdm(map->root);
        const PageSizes selectedSize = (PageSizes)(mode + 1);

        for (size_t i = pagingLevels; i > 0; i--)
        {
            entry = &pt->entries[indices[i]];
            if (i == (size_t)selectedSize)
                break;

            if (!(*entry & PresentFlag))
            {
                const uintptr_t newPt = PMM::Global().Alloc();
                sl::memset(reinterpret_cast<void*>(AddHhdm(newPt)), 0, PageSize);
                *entry = addrMask & newPt;
                *entry |= PresentFlag | (uintptr_t)HatFlags::Write;
            }

            pt = reinterpret_cast<PageTable*>((*entry & addrMask) + hhdmBase);
        }

        //at this point `entry` contains the entry we want, now we can operate on it.
        uint64_t realFlags = ((uint64_t)flags & 0xFFF) | 0b1; //always set present bit (bit 0).
        if (selectedSize > PageSizes::_4K)
            realFlags |= 1 << 7;
        
        //execute flag is backwards on x86: we set it to disable execution for this page,
        //otherwise we leave it clear to enable instruction fetches from the page.
        if (nxSupported && (((uint64_t)flags >> 63) & 0b1) == 0)
            realFlags |= 1ul << 63;
        
        //dont allow global bit to be set if cpu doesn't support global pages
        if (!globalPagesSupported)
            realFlags &= ~(uintptr_t)(1 << 5);

        *entry = realFlags | (paddr & addrMask);
        if (flush)
            asm volatile("invlpg %0" :: "m"(vaddr) : "memory");
        
        //if we modified the kernel map, update the generation count
        if (map == &kernelMap)
            kernelMap.generation++;
        return true;

    }

    bool Unmap(HatMap* map, uintptr_t vaddr, uintptr_t& paddr, size_t& mode, bool flush)
    {
        ASSERT(map != nullptr, "HatMap is nullptr");

        size_t indices[pagingLevels + 1];
        GetAddressIndices(vaddr, indices);

        uint64_t* entry = nullptr;
        PageTable* pt = AddHhdm(map->root);

        for (size_t i = pagingLevels; i > 0; i--)
        {
            entry = &pt->entries[indices[i]];
            if ((*entry & PresentFlag) == 0)
                return false; //translation does not resolve to a physical address
            
            //Two exit conditions are checked: we either reached level 1 (where translation
            //normally ends), or we're at a higher level and bit 7 (size flag) is set, indicating
            //translation should end here.
            //We dont check if it's a valid level for the size flag, as the cpu would have triggered
            //a fault by now if we'd set a reserved bit.
            if ((i > 1 && *entry & (1 << 7)) || i == 1)
            {
                mode = i - 1;
                break;
            }

            pt = reinterpret_cast<PageTable*>((*entry & addrMask) + hhdmBase);
        }

        paddr = *entry & addrMask;
        *entry = 0; //this removes the mapping from the in-memory page tables
        if (flush)
            asm volatile("invlpg %0" :: "m"(vaddr) : "memory");

        //if we modified the kernel map, update the generation count
        if (map == &kernelMap)
            kernelMap.generation++;
        return true;
    }

    sl::Opt<uintptr_t> GetMap(HatMap* map, uintptr_t vaddr)
    {
        ASSERT(map != nullptr, "HatMap is nullptr");

        size_t indices[pagingLevels + 1];
        GetAddressIndices(vaddr, indices);

        //The number of bits used to store to offset into the physical page depends on the
        //level translation ends at (4K pages use a 12-bit offset, 2M use a 21-bit offset),
        //this mask keeps track of what bits we keep for the offset, and what bits we get
        //from the page tables.
        uintptr_t mask = 1;
        uint64_t* entry = nullptr;
        PageTable* pt = AddHhdm(map->root);

        for (size_t i = pagingLevels; i > 0; i--)
        {
            entry = &pt->entries[indices[i]];

            if ((*entry & PresentFlag) == 0)
                return {}; //no translation, return empty Optional<T>
            
            if ((i > 1 && (*entry & (1 << 7))) || i == 1)
            {
                mask <<= (i - 1) * 9 + 12;
                mask--;
                break;
            }

            pt = reinterpret_cast<PageTable*>((*entry & addrMask) + hhdmBase);
        }

        const uintptr_t offset = vaddr & mask;
        const uintptr_t frame = *entry & ~(mask | (1ul << 63)); //make sure we filter out the NX-bit
        return frame | offset;
    }

    void SyncWithMasterMap(HatMap* map)
    {
        ASSERT(map != nullptr, "HatMap is nullptr");

        const PageTable* source = AddHhdm(kernelMap.root);
        PageTable* dest = AddHhdm(map->root);

        for (size_t i = PageTableEntries / 2; i < PageTableEntries; i++)
            dest->entries[i] = source->entries[i];
        map->generation = kernelMap.generation.Load();
    }

    void MakeActiveMap(HatMap* map)
    {
        ASSERT(map != nullptr, "HatMap is nullptr");
        asm volatile("mov %0, %%cr3" :: "r"(map->root) : "memory");
    }

    void HatHandlePanic()
    {
        MakeActiveMap(&kernelMap);
    }
}
