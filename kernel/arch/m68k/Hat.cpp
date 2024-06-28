#include <arch/Hat.h>
#include <debug/Log.h>
#include <memory/Pmm.h>
#include <Memory.h>
#include <Maths.h>

#define PFLUSH(vaddr) do { asm volatile("pflush %0" :: "a"(vaddr) : "memory"); } while(false)
#define PFLUSHA() do { asm volatile("pflushan" ::: "memory"); } while(false)

namespace Npk
{
    constexpr size_t PageTableEntries = 128;
    constexpr size_t PagingLevels = 3;
    constexpr uint32_t ResidentFlag = 3;
    constexpr uint32_t WriteProtectFlag = 1 << 2;
    constexpr uint32_t GlobalFlag = 1 << 10;

    struct PageTable
    {
        uint32_t ptes[PageTableEntries];
    };

    struct HatMap
    {
        PageTable* root;
    };

    struct WalkResult
    {
        size_t level;
        uint32_t* pte;
        bool complete;
    };

    HatMap kernelMap;
    uint32_t tableAddrMask;
    uint32_t descAddrMask;

    constexpr HatLimits limits
    {
        .flushOnPermsUpgrade = false,
        .modeCount = 1,
        .modes = {{ .granularity = PageSize }}
    };

    static inline void GetIndices(uintptr_t vaddr, size_t* indices)
    {
        indices[3] = (vaddr >> 25) & 0x7F;
        indices[2] = (vaddr >> 18) & 0x7F;
        indices[1] = (vaddr >> 12) & 0x3F;
    }

    static inline WalkResult WalkTables(PageTable* root, uintptr_t vaddr)
    {
        size_t indices[PagingLevels + 1];
        GetIndices(vaddr, indices);

        WalkResult result {};
        PageTable* pt = AddHhdm(root);
        for (size_t i = PagingLevels; i > 0; i--)
        {
            result.pte = &pt->ptes[indices[i]];
            result.level = i;
            if ((*result.pte & ResidentFlag) == 0)
            {
                result.complete = false;
                return result;
            }

            pt = reinterpret_cast<PageTable*>((*result.pte & descAddrMask) + hhdmBase);
        }

        result.complete = *result.pte & ResidentFlag;
        return result;
    }

    static inline uint32_t AllocPageTable(uint32_t* pte, size_t index)
    {
        const size_t clusterStride = PageSize / 1;
        const size_t clusterSize = PageSize / clusterStride;
        index = index % clusterSize;

        uint32_t* clusterBase = pte - index;
        if ((*clusterBase & tableAddrMask) == 0)
            *clusterBase = PMM::Global().Alloc();

        return (*clusterBase & tableAddrMask) + (index * clusterStride);
    }
    
    void HatInit()
    {
        //transparent translation regs *should* be zeroed, but clear them
        //anyway (which implies this mechanism is disabled).
        asm("movec %0, %%dtt0" :: "d"(0));
        asm("movec %0, %%dtt1" :: "d"(0));
        asm("movec %0, %%itt0" :: "d"(0));
        asm("movec %0, %%itt1" :: "d"(0));

        uint32_t tcr = 0; //TODO: support 8K pages?
        asm("movec %%tcr, %0" : "=d"(tcr) :: "memory");

        ASSERT((tcr & (1 << 14)) == 0, "8K pages not supported");
        descAddrMask = ~0xFFF;
        tableAddrMask = ~0x1FF;

        kernelMap.root = reinterpret_cast<PageTable*>(PMM::Global().Alloc());
        sl::memset(AddHhdm(kernelMap.root), 0, sizeof(PageTable));

        constexpr HatFlags hhdmFlags = HatFlags::Write | HatFlags::Global;
        for (uintptr_t i = 0; i < hhdmLength; i += PageSize)
            Map(KernelMap(), hhdmBase + i, i, 0, hhdmFlags, false);

        Log("Hat init (paging): levels=3, pageSize=4K", LogLevel::Info);
    }

    const HatLimits& GetHatLimits()
    { return limits; }

    HatMap* InitNewMap()
    {
        HatMap* map = new HatMap();
        map->root = reinterpret_cast<PageTable*>(PMM::Global().Alloc());
        sl::memset(AddHhdm(map), 0, sizeof(PageTable));

        return map;
    }

    void CleanupMap(HatMap* map)
    {
        ASSERT_UNREACHABLE();
    }

    HatMap* KernelMap()
    { return &kernelMap; }

    bool Map(HatMap* map, uintptr_t vaddr, uintptr_t paddr, size_t mode, HatFlags flags, bool flush)
    {
        ASSERT_(map != nullptr);
        if (mode != 0)
            return false;

        size_t indices[PagingLevels + 1];
        GetIndices(vaddr, indices);
        WalkResult path = WalkTables(map->root, vaddr);

        if (path.complete)
            return false;

        while (path.level != 1)
        {
            uint32_t newPt = AllocPageTable(path.pte, indices[path.level]);
            const size_t zeroCount = sizeof(uint32_t) * (path.level == 2 ? 64 : PageTableEntries);
            sl::memset(reinterpret_cast<void*>(AddHhdm(newPt)), 0, zeroCount);
            *path.pte = tableAddrMask & newPt;
            *path.pte |= ResidentFlag;

            path.level--;
            auto pt = reinterpret_cast<PageTable*>(newPt + hhdmBase);
            path.pte = &pt->ptes[indices[path.level]];
        }

        *path.pte = ResidentFlag | (paddr & descAddrMask);
        if ((flags & HatFlags::Write) == HatFlags::None)
            *path.pte |= WriteProtectFlag;
        if ((flags & HatFlags::Global) != HatFlags::None)
            *path.pte |= GlobalFlag;

        if (flush)
            PFLUSH(vaddr);
        return true;
    }

    bool Unmap(HatMap* map, uintptr_t vaddr, uintptr_t& paddr, size_t& mode, bool flush)
    {
        ASSERT_UNREACHABLE();
    }

    sl::Opt<uintptr_t> GetMap(HatMap* map, uintptr_t vaddr, size_t& mode)
    {
        ASSERT_(map != nullptr);

        const WalkResult path = WalkTables(map->root, vaddr);
        if (!path.complete)
            return {};

        mode = 0;
        return (*path.pte & descAddrMask) | (vaddr & ~descAddrMask);
    }

    bool SyncMap(HatMap* map, uintptr_t vaddr, sl::Opt<uintptr_t> paddr, sl::Opt<HatFlags> flags, bool flush)
    {
        ASSERT_UNREACHABLE();
    }

    void MakeActiveMap(HatMap* map, bool supervisor)
    {
        if (supervisor)
            asm("movec %0, %%srp" :: "d"(map->root) : "memory");
        else
            asm("movec %0, %%urp" :: "d"(map->root) : "memory");
        PFLUSHA();
    }
}
