#include <arch/Hat.h>
#include <arch/Cpu.h>
#include <memory/Pmm.h>
#include <debug/Log.h>
#include <Memory.h>

namespace Npk
{
    enum class PageSizes : size_t
    {
        _4K = 1,
        _2M = 2,
        _1G = 3,
    };

    struct PageTable
    {
        uint64_t entries[512];
    };

    struct HatMap
    {
        PageTable* ptroot;
        sl::Atomic<uint32_t> generation;
    };

    constexpr inline size_t GetPageSize(PageSizes size)
    { return 1ul << (12 + 9 * ((size_t)size - 1)); }

    constexpr uint64_t PresentFlag = 0b1;

    size_t pagingLevels;
    uintptr_t addrMask;
    size_t maxTranslationLevel;
    bool nxSupported;
    bool globalPagesSupported;

    HatMap kernelMap;

    void HatInit()
    {
        //Note that we only read the paging mode, the system should have set the appropriate
        //mode long before getting to this stage. This is just querying what was established.
        const uint64_t cr4 = ReadCr4();
        pagingLevels = (cr4 & (1 << 12)) ? 5 : 4; //bit 12 is LA57 (5-level paging).

        maxTranslationLevel = CpuHasFeature(CpuFeature::Pml3Translation) ? 3 : 2;
        nxSupported = CpuHasFeature(CpuFeature::NoExecute);
        globalPagesSupported = CpuHasFeature(CpuFeature::GlobalPages);

        //determine the mask needed to separate the physical address from the flags
        addrMask = 1ul << (9 * pagingLevels + 12);
        addrMask--;
        addrMask &= ~(0xFFFul);

        //create the master set of page tables used for the kernel, stash the details in `kernelMap`.
        kernelMap.generation = 0;
        kernelMap.ptroot = reinterpret_cast<PageTable*>(PMM::Global().Alloc());
        sl::memset(AddHhdm(kernelMap.ptroot), 0, PageSize);

        constexpr const char* SizeStrs[] = { "", "4KiB", "2MiB", "1GiB" };
        Log("Hat init (paging): levels=%lu, maxTranslation=%s, nx=%s, globalPages=%s", LogLevel::Info,
            pagingLevels, SizeStrs[maxTranslationLevel],
            nxSupported ? "yes" : "no", globalPagesSupported ? "yes" : "no");
    }

    HatMap* InitNewMap()
    {
        HatMap* map = new HatMap;
        
        map->generation = kernelMap.generation.Load();
        map->ptroot = reinterpret_cast<PageTable*>(PMM::Global().Alloc());
        sl::memset(AddHhdm(map->ptroot), 0, PageSize / 2);

        SyncWithMasterMap(map);
        return map;
    }

    HatMap* KernelMap()
    {
        return &kernelMap;
    }

    inline void GetAddressIndices(uintptr_t vaddr, size_t* indices)
    {
        if (pagingLevels > 4)
            indices[5] = (vaddr >> 48) & 0x1FF;
        indices[4] = (vaddr >> 39) & 0x1FF;
        indices[3] = (vaddr >> 30) & 0x1FF;
        indices[2] = (vaddr >> 21) & 0x1FF;
        indices[1] = (vaddr >> 12) & 0x1FF;
    }
    
    bool Map(HatMap* map, uintptr_t vaddr, uintptr_t paddr, size_t length, HatFlags flags, bool flush)
    {
        ASSERT(map != nullptr, "HatMap is nullptr");
        if (length > MaxMapLength())
            return false;
        
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
        PageTable* pt = AddHhdm(map->ptroot);
        PageSizes selectedSize = PageSizes::_4K;

        //TODO: use length param to map runs of multiple pages
        for (size_t i = pagingLevels; i > 0; i++)
        {
            entry = &pt->entries[indices[i]];
            //TODO: would be nice to transparently use larger pages here

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
        return true;

    }

    bool Unmap(HatMap* map, uintptr_t vaddr, uintptr_t& paddr, size_t& length, bool flush)
    {
        ASSERT(map != nullptr, "HatMap is nullptr");

        size_t indices[pagingLevels + 1];
        GetAddressIndices(vaddr, indices);

        uint64_t* entry = nullptr;
        PageTable* pt = AddHhdm(map->ptroot);

        for (size_t i = pagingLevels; i > 0; i++)
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
                length = GetPageSize((PageSizes)i);
                break;
            }

            pt = reinterpret_cast<PageTable*>((*entry & addrMask) + hhdmBase);
        }

        paddr = *entry & addrMask;
        *entry = 0; //this removes the mapping from the in-memory page tables

        if (flush)
            asm volatile("invlpg %0" :: "m"(vaddr) : "memory");
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
        PageTable* pt = AddHhdm(map->ptroot);

        for (size_t i = pagingLevels; i > 0; i++)
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
        return frame + offset;
    }

    void SyncWithMasterMap(HatMap* map)
    {
        ASSERT(map != nullptr, "HatMap is nullptr");

        const PageTable* source = AddHhdm(kernelMap.ptroot);
        PageTable* dest = AddHhdm(map->ptroot);

        for (size_t i = 256; i < 512; i++)
            dest->entries[i] = source->entries[i];
    }

    void MakeActiveMap(HatMap* map)
    {
        ASSERT(map != nullptr, "HatMap is nullptr");
        asm volatile("mov %0, %%cr3" :: "r"(map->ptroot) : "memory");
    }

    void HatHandlePanic()
    {
        MakeActiveMap(&kernelMap);
    }

    size_t MaxMapLength()
    {
        //the largest region of memory we can map is half the virtual address space
        //(because of the canonical hole in the middle).
        return addrMask / 2;
    }
}
