#include <arch/Hat.h>
#include <arch/x86_64/Cpuid.h>
#include <memory/Pmm.h>
#include <debug/Log.h>
#include <Memory.h>
#include <Maths.h>

#define INVLPG(vaddr) do { asm volatile("invlpg (%0)" :: "r"(vaddr) : "memory"); } while (false)

namespace Npk
{
    enum class PageSizes : size_t
    {
        _4K = 1,
        _2M = 2,
        _1G = 3,
    };

    constexpr size_t PageTableEntries = 512;
    constexpr size_t MaxPtIndices = 6; //max of 5 levels, +1 because its 1 based counting.

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
    constexpr uint64_t SizeFlag = 1 << 7;
    constexpr uint64_t NxFlag = 1ul << 63;

    size_t pagingLevels;
    uintptr_t addrMask;
    HatMap kernelMap;

    struct 
    {
        bool nx;
        bool globalPages;
    } mmuFeatures;

    HatLimits limits 
    {
        .flushOnPermsUpgrade = false,
        .modeCount = 3,
        .modes = 
        {
            { .granularity = GetPageSize(PageSizes::_4K) },
            { .granularity = GetPageSize(PageSizes::_2M) },
            { .granularity = GetPageSize(PageSizes::_1G) },
        }
    };

    struct WalkResult
    {
        size_t level;
        uint64_t* pte;
        bool complete;
    };

    static inline void GetAddressIndices(uintptr_t vaddr, size_t* indices)
    {
        if (pagingLevels > 4)
            indices[5] = (vaddr >> 48) & 0x1FF;
        indices[4] = (vaddr >> 39) & 0x1FF;
        indices[3] = (vaddr >> 30) & 0x1FF;
        indices[2] = (vaddr >> 21) & 0x1FF;
        indices[1] = (vaddr >> 12) & 0x1FF;
    }

    //Internal helper function, walks the page tables as long as they are valid,
    //returns the PTE where translation ended and at what level.
    static inline WalkResult WalkTables(PageTable* root, uintptr_t vaddr)
    {
        //TODO: use fractal paging to accelerate walks in the same address space
        size_t indices[MaxPtIndices];
        GetAddressIndices(vaddr, indices);

        WalkResult result {};
        PageTable* pt = AddHhdm(root);
        for (size_t i = pagingLevels; i > 0; i--)
        {
            result.pte = &pt->entries[indices[i]];
            result.level = i;
            if ((*result.pte & PresentFlag) == 0)
            {
                result.complete = false;
                return result;
            }
            if (result.level <= limits.modeCount && (*result.pte & SizeFlag) != 0)
            {
                result.complete = true;
                return result;
            }

            pt = reinterpret_cast<PageTable*>((*result.pte & addrMask) + hhdmBase);
        }

        result.complete = *result.pte & PresentFlag;
        return result;
    }

    void HatInit()
    {
        //Note that we only read the paging mode, the system should have set the appropriate
        //mode long before getting to this stage. This is just querying what was established.
        const uint64_t cr4 = ReadCr4();
        pagingLevels = (cr4 & (1 << 12)) ? 5 : 4; //bit 12 is LA57 (5-level paging).

        const size_t maxTranslationLevel = CpuHasFeature(CpuFeature::Pml3Translation) ? 3 : 2;
        if (!CpuHasFeature(CpuFeature::Pml3Translation))
            limits.modeCount = 2; //if the cpu doesn't support gigabyte pages, don't advertise it.
        mmuFeatures.nx = CpuHasFeature(CpuFeature::NoExecute);
        mmuFeatures.globalPages = CpuHasFeature(CpuFeature::GlobalPages);

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
            HatDoMap(KernelMap(), hhdmBase + i, i, hhdmPageSize - 1, hhdmFlags, false);
        
        //adjust HHDM length so matches the memory we mapped, since the VMM uses hhdmLength
        //to know where it can start allocating virtual address space.
        hhdmLength = sl::AlignUp(hhdmLength, GetPageSize((PageSizes)hhdmPageSize));

        constexpr const char* SizeStrs[] = { "", "4KiB", "2MiB", "1GiB" };
        Log("HHDM mapped with %s pages, length adjusted to 0x%lx", LogLevel::Verbose, 
            SizeStrs[hhdmPageSize], hhdmLength);
        Log("Hat init (paging): levels=%lu, maxMapSize=%s, nx=%s, globalPages=%s", LogLevel::Info,
            pagingLevels, SizeStrs[maxTranslationLevel],
            mmuFeatures.nx ? "yes" : "no", mmuFeatures.globalPages ? "yes" : "no");
    }

    const HatLimits& HatGetLimits()
    { return limits; }

    HatMap* HatCreateMap()
    {
        HatMap* map = new HatMap;
        map->root = reinterpret_cast<PageTable*>(PMM::Global().Alloc());
        sl::memset(AddHhdm(map->root), 0, PageSize / 2);

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

    void HatDestroyMap(HatMap* map)
    {
        if (map == nullptr)
            return;

        CleanupPageTable(AddHhdm(map->root));
        delete map;
    }

    HatMap* KernelMap()
    { return &kernelMap; }
    
    bool HatDoMap(HatMap* map, uintptr_t vaddr, uintptr_t paddr, size_t mode, HatFlags flags, bool flush)
    {
        ASSERT_(map != nullptr);
        if (mode >= limits.modeCount)
            return false; //invalid mode
        
        const PageSizes selectedSize = (PageSizes)(mode + 1);
        size_t indices[MaxPtIndices];
        GetAddressIndices(vaddr, indices);
        WalkResult path = WalkTables(map->root, vaddr);

        if (path.complete)
            return false; //translation already exists for this vaddr
        
        //create any additional page tables we need to, to reach our target translation
        //size (and paging level).
        while (path.level != (size_t)selectedSize)
        {
            const uint64_t newPt = PMM::Global().Alloc();
            sl::memset(reinterpret_cast<void*>(AddHhdm(newPt)), 0, PageSize);
            *path.pte = addrMask & newPt;
            *path.pte |= PresentFlag | (uint64_t)HatFlags::Write;

            path.level--;
            auto pt = reinterpret_cast<PageTable*>(newPt + hhdmBase);
            path.pte = &pt->entries[indices[path.level]];
        }

        //start setting flags in the final PTE. Note that the execute flag is backwards on x86:
        //we set it to mark a page as 'no execute', rather than setting the flag to enable instruction
        //fetches from it.
        *path.pte = ((uint64_t)flags & 0xFFF) | PresentFlag;
        *path.pte |= paddr & addrMask;

        if (selectedSize > PageSizes::_4K)
            *path.pte |= SizeFlag;
        if (mmuFeatures.nx && (NxFlag & (uint64_t)flags) == 0)
            *path.pte |= NxFlag;
        if (!mmuFeatures.globalPages)
            *path.pte &= ~(uint64_t)(1 << 5); //global page bit

        //flush the TLB if requested, and update kernel generation count if we need to
        if (flush)
            INVLPG(vaddr);
        if (map == &kernelMap)
            kernelMap.generation++;
        return true;
    }

    bool HatDoUnmap(HatMap* map, uintptr_t vaddr, uintptr_t& paddr, size_t& mode, bool flush)
    {
        ASSERT_(map != nullptr);

        const WalkResult path = WalkTables(map->root, vaddr);
        if (!path.complete)
            return false;

        //populate output variables, clear PTE
        paddr = *path.pte & addrMask;
        mode = path.level - 1;
        *path.pte = 0;

        //flush TLB if requested, and update kernel generation count if required
        if (flush)
            INVLPG(vaddr);
        if (map == &kernelMap && path.level == pagingLevels)
            kernelMap.generation++;

        return true;
    }

    sl::Opt<uintptr_t> HatGetMap(HatMap* map, uintptr_t vaddr, size_t& mode)
    {
        ASSERT_(map != nullptr);

        const WalkResult path = WalkTables(map->root, vaddr);
        if (!path.complete)
            return {};

        mode = path.level - 1;
        const uint64_t offsetMask = GetPageSize((PageSizes)path.level) - 1;
        return (*path.pte & addrMask) | (vaddr & offsetMask);
    }

    bool HatSyncMap(HatMap* map, uintptr_t vaddr, sl::Opt<uintptr_t> paddr, sl::Opt<HatFlags> flags, bool flush)
    {
        ASSERT_(map != nullptr);

        const WalkResult path = WalkTables(map->root, vaddr);
        if (!path.complete)
            return false;

        if (paddr.HasValue())
            *path.pte = (*path.pte & ~addrMask) | (*paddr & addrMask);
        if (flags.HasValue())
        {
            uint64_t newFlags = ((uint64_t)*flags & 0xFFF) | PresentFlag;

            if (path.level > (size_t)PageSizes::_4K)
                newFlags |= PageSize;
            if (mmuFeatures.nx && (NxFlag & (uint64_t)*flags) == 0)
                newFlags |= NxFlag;
            if (!mmuFeatures.globalPages)
                newFlags &= ~(1 << 5);

            *path.pte = (*path.pte & addrMask) | newFlags;
        }

        if (flush)
            INVLPG(vaddr);
        return true;
    }

    static void SyncWithMasterMap(HatMap* map)
    {
        map->generation = kernelMap.generation.Load();

        const PageTable* source = AddHhdm(kernelMap.root);
        PageTable* dest = AddHhdm(map->root);

        for (size_t i = PageTableEntries / 2; i < PageTableEntries; i++)
            dest->entries[i] = source->entries[i];
    }

    void HatMakeActive(HatMap* map, bool supervisor)
    {
        (void)supervisor;

        ASSERT_(map != nullptr);
        if (map != &kernelMap)
            SyncWithMasterMap(map);
        asm volatile("mov %0, %%cr3" :: "r"(map->root) : "memory");
    }
}
