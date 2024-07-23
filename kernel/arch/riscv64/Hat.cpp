#include <arch/Hat.h>
#include <memory/Pmm.h>
#include <debug/Log.h>
#include <Memory.h>
#include <Maths.h>

namespace Npk
{
    enum PageSizes
    {
        _4K = 1,
        _2M = 2,
        _1G = 3,
        _512G = 4,
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

    constexpr uint64_t ValidFlag = 1 << 0;
    constexpr uint64_t ReadFlag = 1 << 1;

    size_t pagingLevels;
    uintptr_t addrMask;
    uintptr_t satpBits; //holds config bits for when we write to satp

    HatMap kernelMap;

    HatLimits limits
    {
        .flushOnPermsUpgrade = false,
        .modeCount = 4,
        .modes = 
        {
            { .granularity = GetPageSize(PageSizes::_4K) },
            { .granularity = GetPageSize(PageSizes::_2M) },
            { .granularity = GetPageSize(PageSizes::_1G) },
            { .granularity = GetPageSize(PageSizes::_512G) },
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
        if (pagingLevels > 3)
            indices[4] = (vaddr >> 39) & 0x1FF;
        indices[3] = (vaddr >> 30) & 0x1FF;
        indices[2] = (vaddr >> 21) & 0x1FF;
        indices[1] = (vaddr >> 12) & 0x1FF;
    }

    static inline WalkResult WalkTables(PageTable* root, uintptr_t vaddr)
    {
        //TODO: use fractal paging to accelerate same-space lookups
        size_t indices[pagingLevels + 1];
        GetAddressIndices(vaddr, indices);

        WalkResult result {};
        PageTable* pt = AddHhdm(root);
        for (size_t i = pagingLevels; i > 0; i--)
        {
            result.pte = &pt->entries[indices[i]];
            result.level = i;
            if ((*result.pte & ValidFlag) == 0)
            {
                result.complete = false;
                return result;
            }
            if ((*result.pte & 0b1110) != 0)
            {
                result.complete = true;
                return result;
            }

            pt = reinterpret_cast<PageTable*>(((*result.pte & addrMask) << 2)+ hhdmBase);
        }

        result.complete = *result.pte & ValidFlag;
        return result;
    }
    
    void HatInit()
    {
        //Note that we're only querying the paging mode here, we dont modify it.
        //All that should have been done by the bootloader (or much earlier in the kernel).
        satpBits = ReadCsr("satp") & (0xFul << 60);
        pagingLevels = (satpBits >> 60) - 5;
        const size_t maxTranslationLevel = sl::Min(pagingLevels, 4ul);
        limits.modeCount = pagingLevels - 1;
        
        //determine the mask needed to extract the PFN from the page table entries.
        addrMask = 1ul << (9 * pagingLevels + 12);
        addrMask--;
        addrMask &= ~0x3FFul;

        //create the master kernel page tables
        kernelMap.generation = 0;
        kernelMap.root = reinterpret_cast<PageTable*>(PMM::Global().Alloc());
        sl::memset(AddHhdm(kernelMap.root), 0, PageSize);

        //find the appropriate page size to use when mapping the HHDM
        ASSERT(hhdmLength >= limits.modes[0].granularity, "HHDM < MMU mode[0]");
        size_t hhdmPageSize = maxTranslationLevel;
        while (GetPageSize((PageSizes)hhdmPageSize) > hhdmLength)
            hhdmPageSize--;
        
        //...and actually map the HHDM.
        constexpr HatFlags hhdmFlags = HatFlags::Write | HatFlags::Global;
        for (uintptr_t i = 0; i < hhdmLength; i += GetPageSize((PageSizes)hhdmPageSize))
            HatDoMap(KernelMap(), hhdmBase + i, i, hhdmPageSize - 1, hhdmFlags, false);
        
        //adjust the HHDM length to match what we mapped, since the VMM will use hhdmLength,
        //to know where it can begin allocating virtual address space.
        hhdmLength = sl::AlignUp(hhdmLength, GetPageSize((PageSizes)hhdmPageSize));

        constexpr const char* SizeStrs[] = { "", "4KiB", "2MiB", "1GiB", "512GiB" };
        Log("HHDM mapped with %s pages", LogLevel::Verbose, SizeStrs[hhdmPageSize]);
        Log("Hat init (paging): levels=%lu, maxMapSize=%s", LogLevel::Info,
            pagingLevels, SizeStrs[maxTranslationLevel]);
    }

    const HatLimits& HatGetLimits()
    { return limits; }

    static void SyncWithMasterMap(HatMap* map)
    {
        const PageTable* source = AddHhdm(kernelMap.root);
        PageTable* dest = AddHhdm(map->root);

        map->generation = kernelMap.generation.Load();
        for (size_t i = PageTableEntries / 2; i < PageTableEntries; i++)
            dest->entries[i] = source->entries[i];
    }

    HatMap* HatCreateMap()
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
            if ((pt->entries[i] & ValidFlag) == 0)
                continue;

            const uint64_t entry = pt->entries[i];
            CleanupPageTable(reinterpret_cast<PageTable*>(((entry & addrMask) << 2) + hhdmBase));
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
            return false;

        const PageSizes selectedSize = (PageSizes)(mode + 1);
        size_t indices[pagingLevels + 1];
        GetAddressIndices(vaddr, indices);
        WalkResult path = WalkTables(map->root, vaddr);

        if (path.complete)
            return false;

        while (path.level != (size_t)selectedSize)
        {
            const uint64_t newPt = PMM::Global().Alloc();
            sl::memset(reinterpret_cast<void*>(AddHhdm(newPt)), 0, PageSize);
            *path.pte = (newPt >> 2) & addrMask;
            *path.pte |= ValidFlag;

            path.level--;
            auto pt = reinterpret_cast<PageTable*>(newPt + hhdmBase);
            path.pte = &pt->entries[indices[path.level]];
        }

        *path.pte = (paddr >> 2) & addrMask;
        *path.pte |= ValidFlag | ReadFlag;
        *path.pte |= (uint64_t)flags & 0x3FF;

        if (flush)
            asm volatile("sfence.vma %0, zero" :: "r"(vaddr) : "memory");
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

        paddr = (*path.pte & addrMask) << 2;
        mode = path.level - 1;
        *path.pte = 0;

        if (flush)
            asm volatile("sfence.vma %0, zero" :: "r"(vaddr) : "memory");
        if (map == &kernelMap)
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
        return((*path.pte & addrMask) << 2) | (vaddr & offsetMask);
    }

    bool HatSyncMap(HatMap* map, uintptr_t vaddr, sl::Opt<uintptr_t> paddr, sl::Opt<HatFlags> flags, bool flush)
    {
        ASSERT_(map != nullptr);

        const WalkResult path = WalkTables(map->root, vaddr);
        if (!path.complete)
            return false;

        if (paddr.HasValue())
            *path.pte = (*path.pte & ~addrMask) | ((*paddr >> 2) & addrMask);
        if (flags.HasValue())
            *path.pte = (*path.pte & addrMask) | ((uint64_t)*flags & 0x3FF) | ReadFlag | ValidFlag;

        if (flush)
            asm volatile("sfence.vma %0, zero" :: "r"(vaddr) : "memory");
        if (map == &kernelMap)
            kernelMap.generation++;
        return true;
    }

    void HatMakeActive(HatMap* map, bool supervisor)
    {
        (void)supervisor;
        ASSERT_(map != nullptr);

        if (map != &kernelMap)
            SyncWithMasterMap(map);
        
        WriteCsr("satp", satpBits | ((uintptr_t)map->root >> 12));
        //writing to satp DOES NOT flush the tlb, this sfence instruction flushes all
        //non-global tlb entries.
        asm volatile("sfence.vma zero, %0" :: "r"(0) : "memory");
    }
}
