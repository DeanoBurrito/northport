#include <hardware/Arch.h>
#include <hardware/x86_64/Cpuid.h>
#include <core/PmAccess.h>
#include <core/Log.h>
#include <Memory.h>

#define INVLPG(vaddr) do { asm volatile("invlpg (%0)" :: "r"(vaddr) : "memory"); } while (false)
#define SET_PTE(pte_ptr, value) do { asm volatile("mov %0, (%1)" :: "r"(value), "r"(pte_ptr)); } while (false)

namespace Npk
{
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

    struct MmuSpace
    {
        Paddr root;
    };

    size_t pagingLevels;
    size_t highestLeafLevel;
    uint64_t addrMask;
    bool nxSupport;
    bool globalPageSupport;
    bool patSupport;

    struct WalkResult
    {
        size_t level;
        Core::PmaRef ptRef;
        uint64_t* pte;
        bool complete;
        bool bad;
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

    static inline WalkResult WalkTables(MmuSpace* space, uintptr_t vaddr)
    {
        WalkResult badResult {};
        badResult.bad = true;

        size_t indices[MaxPtIndices];
        GetAddressIndices(vaddr, indices);

        WalkResult result {};
        Core::PmaRef nextPtRef = Core::GetPmAccess(space->root);
        for (size_t i = pagingLevels; i > 0; i--)
        {
            if (!nextPtRef.Valid())
                return badResult;

            result.pte = &static_cast<PageTable*>(nextPtRef->value)->ptes[indices[i]];
            result.level = i;
            result.ptRef = nextPtRef;

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

            const uint64_t nextPt = *result.pte & addrMask;
            nextPtRef = Core::GetPmAccess(PmLookup(LocalDomain(), nextPt));
        }

        result.complete = *result.pte & PresentFlag;
        return result;
    }

    static void ApplyFlagsToPte(uint64_t& pte, MmuFlags flags)
    {
        if (flags.Has(MmuFlag::Write))
            pte |= WriteFlag;
        if (!flags.Has(MmuFlag::Execute) && nxSupport)
            pte |= NxFlag;
        if (flags.Has(MmuFlag::Global) && globalPageSupport)
            pte |= GlobalFlag;
        if (patSupport)
        {
            if (flags.Has(MmuFlag::Framebuffer))
                pte |= PatWcFlag4K;
            else
                pte |= flags.Has(MmuFlag::Mmio) ? PatUcFlag : 0;
        }
    }

    MmuSpace kernelSpace;

    uintptr_t EarlyMmuBegin(const EarlyMmuEnvironment& env)
    {
        const uint64_t cr4 = ReadCr4();
        pagingLevels = (cr4 & (1 << 12)) ? 5 : 4;

        highestLeafLevel = CpuHasFeature(CpuFeature::Pml3Translation) ? 3 : 2;
        nxSupport = CpuHasFeature(CpuFeature::NoExecute);
        globalPageSupport = CpuHasFeature(CpuFeature::GlobalPages);
        patSupport = CpuHasFeature(CpuFeature::Pat);

        if (!patSupport)
            Log("PAT not supported on this cpu.", LogLevel::Warning);
        Log("MMU init: max-leaf=%zu, nx=%s, global-pages=%s, pat=%s, levels=%zu",
            LogLevel::Info, highestLeafLevel, nxSupport ? "yes" : "no",
            globalPageSupport ? "yes" : "no", patSupport ? "yes" : "no",
            pagingLevels);

        addrMask = 1ul << (9 * pagingLevels + 12);
        addrMask--;
        addrMask &= ~0xFFFul;

        LocalMmuInit();

        kernelSpace.root = env.EarlyPmAlloc();
        sl::MemSet(reinterpret_cast<void*>(kernelSpace.root + env.directMapBase), 0, PageSize());
        env.dom0->kernelSpace = &kernelSpace;

        return -(1ull << (9 * pagingLevels + 11));
    }

    void EarlyMmuMap(const EarlyMmuEnvironment& env, uintptr_t vaddr, uintptr_t paddr, MmuFlags flags)
    {
        size_t indices[MaxPtIndices];
        GetAddressIndices(vaddr, indices);

        auto pt = reinterpret_cast<PageTable*>(kernelSpace.root + env.directMapBase);
        for (size_t i = pagingLevels; i != 1; i--)
        {
            uint64_t* pte = &pt->ptes[indices[i]];
            if ((*pte & PresentFlag) == 0)
            {
                const Paddr page = env.EarlyPmAlloc();
                sl::MemSet(reinterpret_cast<void*>(page + env.directMapBase), 0, PageSize());
                SET_PTE(pte, page | PresentFlag | WriteFlag);
            }
            
            pt = reinterpret_cast<PageTable*>((*pte & addrMask) + env.directMapBase);
        }

        uint64_t pte = (paddr & addrMask) | PresentFlag | GlobalFlag;
        ApplyFlagsToPte(pte, flags);
        SET_PTE(&pt->ptes[indices[1]], pte);
    }

    void EarlyMmuEnd(const EarlyMmuEnvironment& env)
    { 
        (void)env;
    }

    void LocalMmuInit()
    {
        if (nxSupport)
            WriteMsr(Msr::Efer, ReadMsr(Msr::Efer) | (1 << 11));
        if (globalPageSupport)
            WriteCr4(ReadCr4() | (1 << 7));
    }

    void GetMmuCapabilities(MmuCapabilities& caps)
    {
        caps.hwAccessedBit = true;
        caps.hwDirtyBit = true;
    }

    MmuSpace* CreateMmuSpace()
    { ASSERT_UNREACHABLE(); }

    void DestroyMmuSpace(MmuSpace** space)
    { ASSERT_UNREACHABLE(); }

    MmuError MmuMap(MmuSpace* space, void* vaddr, Paddr paddr, MmuFlags flags)
    {
        VALIDATE_(space != nullptr, MmuError::InvalidArg);

        PageInfo* pages[MaxPtIndices];
        for (size_t i = 0; i < MaxPtIndices; i++)
            pages[i] = nullptr;

        size_t indices[MaxPtIndices];
        GetAddressIndices(reinterpret_cast<uintptr_t>(vaddr), indices);

        WalkResult path = WalkTables(space, reinterpret_cast<uintptr_t>(vaddr));
        VALIDATE_(!path.bad, MmuError::InvalidArg);
        VALIDATE_(!path.complete, MmuError::MapAlreadyExists);

        while (path.level != 1)
        {
            auto newPage = PmAlloc(LocalDomain());
            if (!newPage.HasValue())
            {
                for (size_t i = 0; i < MaxPtIndices; i++)
                {
                    if (pages[i] == nullptr)
                        break;
                    PmFree(LocalDomain(), pages[i]);
                }
                return MmuError::PmAllocFailed;
            }
            PageInfo* page = *newPage;
            page->mmu.validCount = 0;
            pages[path.level] = page;

            PmLookup(LocalDomain(), path.ptRef->key)->mmu.validCount++;
            SET_PTE(path.pte, PmRevLookup(LocalDomain(), page) | PresentFlag | WriteFlag);
            path.level--;

            path.ptRef = Core::GetPmAccess(page);
            ASSERT_(path.ptRef.Valid());
            path.pte = &static_cast<PageTable*>(path.ptRef->value)->ptes[indices[path.level]];
        }

        uint64_t pte = (paddr & addrMask) | PresentFlag;
        ApplyFlagsToPte(pte, flags);

        SET_PTE(path.pte, pte);
        return MmuError::Success;
    }

    sl::ErrorOr<MmuMapping, MmuError> MmuUnmap(MmuSpace* space, void* vaddr)
    {
        VALIDATE_(space != nullptr, MmuError::InvalidArg);

        WalkResult path = WalkTables(space, reinterpret_cast<uintptr_t>(vaddr));
        VALIDATE_(!path.bad, MmuError::InvalidArg);
        VALIDATE_(path.complete, MmuError::NoExistingMap);

        MmuMapping mapping {};
        mapping.paddr = *path.pte & addrMask;
        mapping.accessed = *path.pte & AccessedFlag;
        mapping.dirty = *path.pte & DirtyFlag;

        SET_PTE(path.pte, 0);
        INVLPG(reinterpret_cast<uintptr_t>(vaddr));

        //TODO: check if we can free any PTs used in this translation (walk back up the tree, PageInfo::mmu.validCount == 0)
        return mapping;
    }

    sl::ErrorOr<MmuMapping, MmuError> MmuGetMap(MmuSpace* space, void* vaddr)
    {
        VALIDATE_(space != nullptr, MmuError::InvalidArg);

        const WalkResult path = WalkTables(space, reinterpret_cast<uintptr_t>(vaddr));
        VALIDATE_(!path.bad, MmuError::InvalidArg);
        VALIDATE_(path.complete, MmuError::NoExistingMap);

        MmuMapping mapping {};
        mapping.paddr = *path.pte & addrMask;
        mapping.accessed = *path.pte & AccessedFlag;
        mapping.dirty = *path.pte & DirtyFlag;

        return mapping;
    }

    MmuError MmuSetMap(MmuSpace* space, void* vaddr, sl::Opt<Paddr> paddr, sl::Opt<MmuFlags> flags)
    {
        VALIDATE_(space != nullptr, MmuError::InvalidArg);

        const WalkResult path = WalkTables(space, reinterpret_cast<uintptr_t>(vaddr));
        VALIDATE_(!path.bad, MmuError::InvalidArg);
        VALIDATE_(path.complete, MmuError::NoExistingMap);

        uint64_t newPte = paddr.HasValue() ? *paddr : (*path.pte & addrMask);
        
        if (flags.HasValue())
            ApplyFlagsToPte(newPte, *flags);
        else
            newPte |= *path.pte & ~addrMask;

        SET_PTE(path.pte, newPte);
        INVLPG(reinterpret_cast<uintptr_t>(vaddr));

        return MmuError::Success;
    }

    void MmuFlushCaches(MmuSpace* space, sl::Opt<void*> vaddr)
    {
        ASSERT_(space != nullptr);

        if (vaddr.HasValue())
            INVLPG(*vaddr);
        else
            WriteCr3(space->root);
    }

    void MmuActivate(MmuSpace* space, bool supervisor)
    {
        ASSERT_(supervisor); //TODO: userspace mappings
        ASSERT_(space != nullptr);

        WriteCr3(space->root);
    }
}
