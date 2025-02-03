#include <arch/Hat.h>
#include <arch/Misc.h>
#include <arch/x86_64/Cpuid.h>
#include <core/Log.h>
#include <core/Pmm.h>
#include <core/WiredHeap.h>
#include <interfaces/intra/LinkerSymbols.h>
#include <interfaces/loader/Generic.h>
#include <Hhdm.h>
#include <Maths.h>
#include <Memory.h>
#include <Atomic.h>
#include <NativePtr.h>

#define INVLPG(vaddr) do { asm volatile("invlpg (%0)" :: "r"(vaddr) : "memory"); } while (false)
#define SET_PTE(pte_ptr, value) do { asm volatile("mov %0, (%1)" :: "r"(value), "r"(pte_ptr)); } while (false)
#define INVLPGB(vaddr) do { asm volatile("invlpgb" :: "a"(vaddr | 1), "c"(0), "d"(0)); } while (false)
#define TLBSYNC() asm("tlbsync")

namespace Npk
{
    enum class PageSizes : size_t
    {
        _4K = 1,
        _2M = 2,
        _1G = 3,
    };

    constexpr size_t PtEntries = 512;
    constexpr size_t MaxPtIndices = 6;
    constexpr uint64_t PresentFlag = 1 << 0;
    constexpr uint64_t WriteFlag = 1 << 1;
    constexpr uint64_t UserFlag = 1 << 2;
    constexpr uint64_t DirtyFlag = 1 << 5;
    constexpr uint64_t AccessedFlag = 1 << 6;
    constexpr uint64_t SizeFlag = 1 << 7;
    constexpr uint64_t GlobalFlag = 1 << 8;
    constexpr uint64_t NxFlag = 1ul << 63;
    constexpr uint64_t PatUcFlag = (1 << 4) | (1 << 3); //(3) for strong uncachable
    constexpr uint64_t PatWcFlag4K = (1 << 7) | (1 << 3); //(5) for write-combining
    constexpr uint64_t PatWcFlag2M1G = (1 << 12) | (1 << 3); //(5) for write-combining

    struct PageTable
    {
        uint64_t ptes[PtEntries];
    };

    struct HatMap
    {
        PageTable* root;
        sl::Atomic<size_t> generation;
    };

    constexpr inline size_t GetPageSize(PageSizes size)
    { return 1ul << (12 + 9 * ((size_t)size - 1)); }

    size_t pagingLevels;
    size_t highestLeafLevel;
    uintptr_t addrMask;
    HatMap kernelMap;
    bool nxSupport;
    bool globalPageSupport;
    bool patSupport;
    bool invlpgbSupport;

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
        size_t indices[MaxPtIndices];
        GetAddressIndices(vaddr, indices);

        WalkResult result {};
        PageTable* pt = AddHhdm(root);
        for (size_t i = pagingLevels; i > 0; i--)
        {
            result.pte = &pt->ptes[indices[i]];
            result.level = i;
            if ((*result.pte & PresentFlag) == 0)
            {
                result.complete = false;
                return result;
            }
            if (result.level <= highestLeafLevel && (*result.pte & SizeFlag) != 0)
            {
                result.complete = true;
                return result;
            }

            pt = reinterpret_cast<PageTable*>((*result.pte & addrMask) + hhdmBase);
        }

        result.complete = *result.pte & PresentFlag;
        return result;
    }

    static void EarlyMap(uintptr_t vaddr, uintptr_t paddr, PageSizes size, uint64_t flags)
    {
        size_t indices[MaxPtIndices];
        GetAddressIndices(vaddr, indices);
        WalkResult path = WalkTables(kernelMap.root, vaddr);
        ASSERT_(!path.complete);

        while (path.level != (size_t)size)
        {
            auto maybePt = EarlyPmAlloc(sizeof(PageTable));
            ASSERT_(maybePt.HasValue());

            sl::MemSet(reinterpret_cast<void*>(AddHhdm(*maybePt)), 0, sizeof(PageTable));
            SET_PTE(path.pte, (addrMask & *maybePt) | PresentFlag | WriteFlag);

            path.level--;
            auto pt = reinterpret_cast<PageTable*>(AddHhdm(*maybePt));
            path.pte = &pt->ptes[indices[path.level]];
        }

        SET_PTE(path.pte, PresentFlag | flags | (paddr & addrMask) | (size != PageSizes::_4K ? SizeFlag : 0));
    }

    void HatInit(bool firstCall)
    {
        if (firstCall)
        {
            const uint64_t cr4 = ReadCr4();
            pagingLevels = (cr4 & (1 << 12)) ? 5 : 4; //bit 12 is LA57 (5-level paging).

            highestLeafLevel = CpuHasFeature(CpuFeature::Pml3Translation) ? 3 : 2;
            nxSupport = CpuHasFeature(CpuFeature::NoExecute);
            globalPageSupport = CpuHasFeature(CpuFeature::GlobalPages);
            patSupport = CpuHasFeature(CpuFeature::Pat);
            invlpgbSupport = CpuHasFeature(CpuFeature::BroadcastInvlpg);
        }

        if (!patSupport)
            Log("PAT not supported on this cpu.", LogLevel::Warning);
        if (nxSupport)
            WriteMsr(MsrEfer, ReadMsr(MsrEfer) | (1 << 11));
        if (globalPageSupport)
            WriteCr4(ReadCr4() | (1 << 7)); //tell the cpu we want to use global pages

        if (!firstCall)
            return;

        //determine the mask needed to separate the physical address from the flags
        addrMask = 1ul << (9 * pagingLevels + 12);
        addrMask--;
        addrMask &= ~0xFFFul;

        //create master set of page tables for the kernel. There are a few things we need to map in the
        //kernel map:
        //- the kernel image itself
        //- the hhdm
        //- the page info database
        auto maybePage = EarlyPmAlloc(PageSize());
        ASSERT_(maybePage.HasValue());
        kernelMap.root = reinterpret_cast<PageTable*>(*maybePage);
        sl::MemSet(AddHhdm(kernelMap.root), 0, sizeof(PageTable));

        //first we map the hhdm
        const size_t maxTranslationLevel = CpuHasFeature(CpuFeature::Pml3Translation) ? 3 : 2;
        size_t hhdmPageSize = maxTranslationLevel;
        while (GetPageSize((PageSizes)hhdmPageSize) > hhdmLength)
            hhdmPageSize--;

        const uint64_t hhdmFlags = WriteFlag | (globalPageSupport ? GlobalFlag : 0) | (nxSupport ? NxFlag : 0);
        for (size_t i = 0; i < hhdmLength; i += GetPageSize((PageSizes)hhdmPageSize))
            EarlyMap(hhdmBase + i, i, (PageSizes)hhdmPageSize, hhdmFlags);
        hhdmLength = sl::AlignUp(hhdmLength, GetPageSize((PageSizes)hhdmPageSize));

        constexpr const char* PageSizeStrs[] = { "", "4KiB", "2MiB", "1GiB" };
        Log("Hhdm mapped with %s pages, length adjusted to 0x%zx", LogLevel::Info,
            PageSizeStrs[hhdmPageSize], hhdmLength);

        //next map the kernel image
        const uintptr_t virtBase = (uintptr_t)KERNEL_BLOB_BEGIN;
        const uintptr_t physBase = GetKernelPhysAddr();
        const size_t imageSize = (uintptr_t)KERNEL_BLOB_END - (uintptr_t)KERNEL_BLOB_BEGIN;
        uint64_t imageFlags = globalPageSupport ? GlobalFlag : 0;

        for (char* i = sl::AlignDown(KERNEL_TEXT_BEGIN, 0x1000); i < KERNEL_TEXT_END; i += 0x1000)
            EarlyMap((uintptr_t)i, (uintptr_t)i - virtBase + physBase, PageSizes::_4K, imageFlags);

        imageFlags |= nxSupport ? NxFlag : 0;
        for (char* i = sl::AlignDown(KERNEL_RODATA_BEGIN, 0x1000); i < KERNEL_RODATA_END; i += 0x1000)
            EarlyMap((uintptr_t)i, (uintptr_t)i - virtBase + physBase, PageSizes::_4K, imageFlags);

        imageFlags |= WriteFlag;
        for (char* i = sl::AlignDown(KERNEL_DATA_BEGIN, 0x1000); i < KERNEL_DATA_END; i += 0x1000)
            EarlyMap((uintptr_t)i, (uintptr_t)i - virtBase + physBase, PageSizes::_4K, imageFlags);
        Log("Kernel image mapped: vbase=0x%tx pbase=0x%tx size=0x%tx", LogLevel::Info,
            virtBase, physBase, imageSize);
        
        //and the page info database
        const uintptr_t dbBase = hhdmBase + hhdmLength;
        const size_t structsPerPage = PageSize() / sizeof(Core::PageInfo);
        constexpr size_t _2MiB = 2 * MiB;
        constexpr size_t _2MiBMask = _2MiB - 1;

        MemmapEntry currEntry {};
        size_t currEntryIndex = 0;
        uintptr_t lastMapped = dbBase;
        while (true)
        {
            if (GetUsableMemmap({ &currEntry, 1 }, currEntryIndex++) == 0)
                break;

            const uintptr_t base = dbBase + currEntry.base / structsPerPage;
            const uintptr_t length = currEntry.length / structsPerPage;
            const uintptr_t top = AlignUpPage(base + length);

            lastMapped = sl::Max(lastMapped, AlignDownPage(base));
            PageSizes mapSize = PageSizes::_4K;
            sl::Opt<uintptr_t> maybePage {};
            if ((lastMapped & _2MiBMask) == 0 && (lastMapped + _2MiB) <= top)
            {
                if ((maybePage = EarlyPmAlloc(_2MiB)).HasValue())
                    mapSize = PageSizes::_2M;
            }
            if (!maybePage.HasValue())
                maybePage = EarlyPmAlloc(4 * KiB);

            for (; lastMapped < top; lastMapped += (mapSize == PageSizes::_2M) ? _2MiB : (4 * KiB))
                EarlyMap(lastMapped, *EarlyPmAlloc(0x1000), PageSizes::_4K, imageFlags);
        }

        Log("PageInfo database mapped: base=0x%tx length=0x%zx", LogLevel::Info, dbBase, lastMapped - dbBase);
    }

    void HatGetCapabilities(HatCapabilities& caps)
    {
        caps.tlbFlushBroadcast = invlpgbSupport;
        caps.hwDirtyBit = true;
        caps.hwAccessBit = true;
    }

    HatMap* HatCreateMap()
    {
        auto rootPt = Core::PmAlloc();
        if (!rootPt.HasValue())
            return nullptr;

        HatMap* map = NewWired<HatMap>();
        if (map == nullptr)
        {
            Core::PmFree(*rootPt);
            return nullptr;
        }

        map->generation = 0;
        map->root = reinterpret_cast<PageTable*>(*rootPt);

        return map;
    }

    void HatDestroyMap(HatMap* map)
    {
        if (map == nullptr)
            return;

        Core::PmFree(reinterpret_cast<uintptr_t>(map->root));
        DeleteWired(map);
    }
    
    HatMap* KernelMap()
    {
        return &kernelMap;
    }

    HatError HatDoMap(HatMap* map, uintptr_t vaddr, uintptr_t paddr, size_t mode, HatFlags flags)
    {
        VALIDATE_(map != nullptr, HatError::InvalidArg);
        VALIDATE_(mode < highestLeafLevel, HatError::InvalidArg);

        uintptr_t pmAllocs[MaxPtIndices]; //allows us to free intermediate page tables allocated if
        for (size_t i = 0; i < MaxPtIndices; i++) //the mapping fails later.
            pmAllocs[i] = 0;

        const PageSizes selectedSize = (PageSizes)(mode + 1);
        size_t indices[MaxPtIndices];
        GetAddressIndices(vaddr, indices);

        WalkResult path = WalkTables(map->root, vaddr);
        VALIDATE_(!path.complete, HatError::MapAlreadyExists);

        while (path.level != (size_t)selectedSize)
        {
            auto newPt = Core::PmAlloc();
            if (!newPt.HasValue())
            {
                for (size_t i = 0; i < MaxPtIndices; i++)
                {
                    if (pmAllocs[i] != 0)
                        Core::PmFree(pmAllocs[i]);
                }
                return HatError::PmAllocFailed;
            }

            const uint64_t pt = *newPt;
            pmAllocs[path.level] = pt;
            sl::MemSet(reinterpret_cast<void*>(AddHhdm(pt)), 0, sizeof(PageTable));
            SET_PTE(path.pte, pt | PresentFlag | WriteFlag);

            path.level--;
            auto nextPt = reinterpret_cast<PageTable*>(pt + hhdmBase);
            path.pte = &nextPt->ptes[indices[path.level]];
        }

        uint64_t pte = (paddr & addrMask) | PresentFlag;
        if (flags.Has(HatFlag::Write))
            pte |= WriteFlag;
        if (!flags.Has(HatFlag::Execute) && nxSupport)
            pte |= NxFlag;
        if (flags.Has(HatFlag::Global) && globalPageSupport)
            pte |= GlobalFlag;
        if (selectedSize > PageSizes::_4K)
            pte |= SizeFlag;
        if (patSupport)
        {
            if (flags.Has(HatFlag::Framebuffer))
                pte |= path.level > 1 ? PatWcFlag2M1G : PatWcFlag4K;
            else
                pte |= flags.Has(HatFlag::Mmio) ? PatUcFlag : 0;
        }

        SET_PTE(path.pte, pte);

        if (map == &kernelMap) //TODO: optimize by only incrementing on new PML3 alloc (can check pmAllocs[3] != 0)
            kernelMap.generation++;

        return HatError::Success;
    }

    HatError HatDoUnmap(HatMap* map, uintptr_t vaddr, uintptr_t& paddr, size_t& mode)
    {
        VALIDATE_(map != nullptr, HatError::InvalidArg);

        const WalkResult path = WalkTables(map->root, vaddr);
        VALIDATE_(path.complete, HatError::NoExistingMap);

        paddr = *path.pte & addrMask;
        mode = path.level - 1;
        SET_PTE(path.pte, 0);

        INVLPG(vaddr);
        if (map == &kernelMap && path.level == pagingLevels)
            kernelMap.generation++;

        return HatError::Success;
    }

    sl::ErrorOr<uintptr_t, HatError> HatGetMap(HatMap* map, uintptr_t vaddr, size_t& mode)
    {
        VALIDATE_(map != nullptr, HatError::InvalidArg);

        const WalkResult path = WalkTables(map->root, vaddr);
        if (!path.complete)
            return HatError::NoExistingMap;

        mode = path.level - 1;
        const uint64_t offsetMask = GetPageSize((PageSizes)path.level) - 1;
        return (*path.pte & addrMask) | (vaddr & offsetMask);
    }

    sl::ErrorOr<bool, HatError> HatGetDirty(HatMap* map, uintptr_t vaddr, bool clear)
    {
        VALIDATE_(map != nullptr, HatError::InvalidArg);

        const WalkResult path = WalkTables(map->root, vaddr);
        VALIDATE_(path.complete, HatError::NoExistingMap);

        const bool set = *path.pte & DirtyFlag;
        if (clear)
        {
            const uint64_t pte = *path.pte & ~DirtyFlag;
            SET_PTE(path.pte, pte);
            INVLPG(vaddr);
        }

        return set;
    }

    sl::ErrorOr<bool, HatError> HatGetAccessed(HatMap* map, uintptr_t vaddr, bool clear)
    {
        VALIDATE_(map != nullptr, HatError::InvalidArg);

        const WalkResult path = WalkTables(map->root, vaddr);
        VALIDATE_(path.complete, HatError::NoExistingMap);

        const bool set = *path.pte & AccessedFlag;
        if (clear)
        {
            const uint64_t pte = *path.pte & ~AccessedFlag;
            SET_PTE(*path.pte, pte);
            INVLPG(vaddr);
        }

        return set;
    }

    HatError HatSyncMap(HatMap* map, uintptr_t vaddr, sl::Opt<uintptr_t> paddr, sl::Opt<HatFlags> flags, bool flush)
    {
        VALIDATE_(map != nullptr, HatError::InvalidArg);

        const WalkResult path = WalkTables(map->root, vaddr);
        VALIDATE_(path.complete, HatError::NoExistingMap);

        if (paddr.HasValue())
            SET_PTE(path.pte, (*path.pte & ~addrMask) | (*paddr & addrMask));
        if (flags.HasValue())
        {
            uint64_t newFlags = PresentFlag;
            if (path.level > (size_t)PageSizes::_4K)
                newFlags |= SizeFlag;

            if (flags->Has(HatFlag::Write))
                newFlags |= WriteFlag;
            if (!flags->Has(HatFlag::Execute) && nxSupport)
                newFlags |= NxFlag;
            if (flags->Has(HatFlag::Global) && globalPageSupport)
                newFlags |= GlobalFlag;

            SET_PTE(path.pte, (*path.pte & addrMask) | newFlags);
        }

        if (flush)
            INVLPG(vaddr);
        return HatError::Success;
    }

    bool HatFlushBroadcast(uintptr_t vaddr)
    {
        if (!invlpgbSupport)
            return false;

        INVLPGB(vaddr);
        TLBSYNC();
        return true;
    }

    void HatFlushMap(uintptr_t vaddr)
    {
        INVLPG(vaddr);
    }

    static void SyncWithKernelMap(HatMap* map)
    {
        map->generation = kernelMap.generation.Load();

        const PageTable* source = AddHhdm(kernelMap.root);
        PageTable* dest = AddHhdm(map->root);

        for (size_t i = PtEntries / 2; i < PtEntries; i++)
            SET_PTE(dest->ptes + i, source->ptes[i]);
    }

    void HatMakeActive(HatMap* map, bool supervisor)
    {
        (void)supervisor;
        VALIDATE_(map != nullptr, );

        if (map != &kernelMap)
            SyncWithKernelMap(map);
        WriteCr3(reinterpret_cast<uint64_t>(map->root));
    }
}
