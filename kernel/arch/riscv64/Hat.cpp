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
        .modeCount = 4,
        .modes = 
        {
            { .granularity = GetPageSize(PageSizes::_4K) },
            { .granularity = GetPageSize(PageSizes::_2M) },
            { .granularity = GetPageSize(PageSizes::_1G) },
            { .granularity = GetPageSize(PageSizes::_512G) },
        }
    };
    
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
            Map(KernelMap(), hhdmBase + i, i, hhdmPageSize - 1, hhdmFlags, false);
        
        //adjust the HHDM length to match what we mapped, since the VMM will use hhdmLength,
        //to know where it can begin allocating virtual address space.
        hhdmLength = sl::AlignUp(hhdmLength, GetPageSize((PageSizes)hhdmPageSize));

        constexpr const char* SizeStrs[] = { "", "4KiB", "2MiB", "1GiB", "512GiB" };
        Log("HHDM mapped with %s pages", LogLevel::Verbose, SizeStrs[hhdmPageSize]);
        Log("Hat init (paging): levels=%lu, maxMapSize=%s", LogLevel::Info,
            pagingLevels, SizeStrs[maxTranslationLevel]);
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
            if ((pt->entries[i] & ValidFlag) == 0)
                continue;

            const uint64_t entry = pt->entries[i];
            CleanupPageTable(reinterpret_cast<PageTable*>(((entry & addrMask) << 2) + hhdmBase));
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
        if (pagingLevels > 3)
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

        uint64_t* entry = nullptr;
        PageTable* pt = AddHhdm(map->root);
        const PageSizes selectedSize = (PageSizes)(mode + 1);

        for (size_t i = pagingLevels; i > 0; i--)
        {
            entry = &pt->entries[indices[i]];
            if (i == selectedSize)
                break;
            
            if (!(*entry & ValidFlag))
            {
                const uintptr_t newPt = PMM::Global().Alloc();
                sl::memset(reinterpret_cast<void*>(AddHhdm(newPt)), 0, PageSize);
                *entry = (newPt >> 2) & addrMask;
                *entry |= ValidFlag;
            }

            pt = reinterpret_cast<PageTable*>(((*entry & addrMask) << 2) + hhdmBase);
        }

        *entry = (paddr >> 2) & addrMask;
        *entry |= ValidFlag | ReadFlag | ((uintptr_t)flags & 0x3FF);
        if (flush)
            asm volatile ("sfence.vma %0, zero" :: "r"(vaddr) : "memory");
        
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

        for (size_t i = 0; i < pagingLevels; i--)
        {
            entry = &pt->entries[indices[i]];
            if ((*entry & ValidFlag) == 0)
                return false; //translation fails to resolve
            
            if (*entry &0b1110)
            {
                mode = i - 1;
                break;
            }

            pt = reinterpret_cast<PageTable*>(((*entry & addrMask) << 2) + hhdmBase);
        }

        paddr = (*entry & addrMask) << 2;
        *entry = 0;

        if (flush)
            asm volatile("sfence.vma %0, zero" :: "r"(vaddr) : "memory");
        return true;
    }

    sl::Opt<uintptr_t> GetMap(HatMap* map, uintptr_t vaddr)
    {
        ASSERT(map != nullptr, "HatMap is nullptr");

        size_t indices[pagingLevels + 1];
        GetAddressIndices(vaddr, indices);

        uintptr_t mask = 1;
        uint64_t* entry = nullptr;
        PageTable* pt = AddHhdm(map->root);
        
        for (size_t i = pagingLevels; i > 0; i--)
        {
            entry = &pt->entries[indices[i]];
            if ((*entry & ValidFlag) == 0)
                return {}; //no translation, return empty
            
            if (*entry & 0b1110)
            {
                mask <<= ((i - 1) * 9) + 12;
                mask--;
                break;

                pt = reinterpret_cast<PageTable*>(((*entry & addrMask) << 2) + hhdmBase);
            }
        }

        const uintptr_t offset = vaddr & mask;
        const uintptr_t frame = ((*entry & addrMask) << 2) & ~mask;
        return offset | frame;
    }

    void SyncWithMasterMap(HatMap* map)
    {
        ASSERT(map != nullptr, "HatMap is nullptr");

        const PageTable* source = AddHhdm(kernelMap.root);
        PageTable* dest = AddHhdm(map->root);

        map->generation = kernelMap.generation.Load();
        for (size_t i = PageTableEntries / 2; i < PageTableEntries; i++)
            dest->entries[i] = source->entries[i];
    }

    void MakeActiveMap(HatMap* map)
    {
        ASSERT(map != nullptr, "HatMap is nullptr");
        
        WriteCsr("satp", satpBits | ((uintptr_t)map->root >> 12));
        //writing to satp DOES NOT flush the tlb, this sfence instruction flushes all
        //non-global tlb entries.
        asm volatile("sfence.vma zero, %0" :: "r"(0) : "memory");
    }

    void HatHandlePanic()
    {
        MakeActiveMap(&kernelMap);
    }
}
