#include <hardware/Arch.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <hardware/x86_64/Mmu.hpp>
#include <KernelApi.hpp>
#include <Memory.h>
#include <Maths.h>

extern "C"
{
    extern char SpinupBlob[];
    extern char _EndOfSpinupBlob[];
}

#define INVLPG(vaddr) do { asm volatile("invlpg (%0)" :: "r"(vaddr) : "memory"); } while (false)
#define SET_PTE(pte_ptr, value) do { asm volatile("mov %0, (%1)" :: "r"(value), "r"(pte_ptr)); } while (false)

namespace Npk
{
    constexpr size_t PtEntries = 512;
    constexpr size_t MaxPtIndices = 6;

    constexpr uint64_t PresentFlag = 1 << 0;
    constexpr uint64_t WriteFlag = 1 << 1;
    constexpr uint64_t GlobalFlag = 1 << 8;
    constexpr uint64_t UserFlag = 1 << 2;
    constexpr uint64_t DirtyFlag = 1 << 5;
    constexpr uint64_t AccessedFlag = 1 << 6;
    constexpr uint64_t NxFlag = 1ul << 63;
    constexpr uint64_t PatUcFlag = (1 << 4) | (1 << 3); //(3) for strong uncachable
    constexpr uint64_t PatWcFlag = (1 << 7) | (1 << 3); //(5) for write-combining

    uint64_t addrMask;
    size_t ptLevels;
    bool nxSupport;
    bool globalPageSupport;
    bool patSupport;

    Paddr kernelMap;
    Paddr apBootPage;
    uintptr_t tempMapBase;
    sl::Span<uint64_t> tempMapAccess;

    struct PageTable
    {
        uint64_t ptes[PtEntries];
    };

    static inline void GetAddressIndices(uintptr_t vaddr, size_t* indices)
    {
        indices[5] = (vaddr >> 48) & 0x1FF;
        indices[4] = (vaddr >> 39) & 0x1FF;
        indices[3] = (vaddr >> 30) & 0x1FF;
        indices[2] = (vaddr >> 21) & 0x1FF;
        indices[1] = (vaddr >> 12) & 0x1FF;
    }

    static void ApplyFlagsToPte(uint64_t& pte, MmuFlags flags)
    {
        if (flags.Has(MmuFlag::Write))
            pte |= WriteFlag;
        if (!flags.Has(MmuFlag::Fetch) && nxSupport)
            pte |= NxFlag;
        if (patSupport)
        {
            if (flags.Has(MmuFlag::Framebuffer))
                pte |= PatWcFlag;
            else
                pte |= flags.Has(MmuFlag::Mmio) ? PatUcFlag : 0;
        }
    }

    //internal-use only, returns the paddr of the last-level page-table
    static Paddr DoEarlyMap(InitState& state, Paddr paddr, uintptr_t vaddr, MmuFlags flags)
    {
        size_t indices[MaxPtIndices];
        GetAddressIndices(vaddr, indices);

        auto pt = reinterpret_cast<PageTable*>(kernelMap + state.dmBase);
        for (size_t i = ptLevels; i != 1; i--)
        {
            uint64_t* pte = &pt->ptes[indices[i]];
            if ((*pte & PresentFlag) == 0)
                SET_PTE(pte, state.PmAlloc() | PresentFlag | WriteFlag);

            pt = reinterpret_cast<PageTable*>((*pte & addrMask) + state.dmBase);
        }

        uint64_t pte = (paddr & addrMask) | PresentFlag;
        //if (vaddr >= (static_cast<uintptr_t>(~0) >> 1))
            //pte |= GlobalFlag; //only set global flag for higher-half mappings

        ApplyFlagsToPte(pte, flags);
        SET_PTE(&pt->ptes[indices[1]], pte);

        return reinterpret_cast<Paddr>(pt) - state.dmBase;
    }

    uintptr_t ArchInitBspMmu(InitState& state, size_t tempMapCount)
    {
        const uint64_t cr4 = READ_CR(4);
        ptLevels = (cr4 & (1 << 12)) ? 5 : 4;

        nxSupport = CpuHasFeature(CpuFeature::NoExecute);
        globalPageSupport = CpuHasFeature(CpuFeature::GlobalPages);
        patSupport = CpuHasFeature(CpuFeature::Pat);

        if (!patSupport)
            Log("PAT not supported on this cpu.", LogLevel::Warning);

        addrMask = 1ull << (9 * ptLevels + 12);
        addrMask--;
        addrMask &= ~0xFFFul;

        kernelMap = state.PmAlloc();
        domain0.kernelSpace = kernelMap;
        //TODO: map hhdm as pageaccess optimization

        apBootPage = state.PmAlloc();
        ArchEarlyMap(state, apBootPage, apBootPage, MmuFlag::Write | MmuFlag::Fetch);

        const size_t blobLength = (uintptr_t)_EndOfSpinupBlob - (uintptr_t)SpinupBlob;
        NPK_ASSERT(blobLength <= PageSize());
        sl::MemCopy(reinterpret_cast<void*>(apBootPage + state.dmBase), SpinupBlob, blobLength);
        Log("AP boot blob @ 0x%tx", LogLevel::Verbose, apBootPage);

        uintptr_t vmAllocHead = -(1ull << (9 * ptLevels + 11)); 

        tempMapBase = vmAllocHead;
        tempMapCount = sl::AlignUp(tempMapCount, PtEntries);
        vmAllocHead += tempMapCount << PfnShift();
        tempMapAccess = { reinterpret_cast<uint64_t*>(vmAllocHead), tempMapCount };

        for (size_t i = 0; i < tempMapCount; i++)
        {
            const Paddr pt = DoEarlyMap(state, 0, tempMapBase + (i << PfnShift()), MmuFlag::Write);

            if ((i & (PtEntries - 1)) == 0)
            {
                DoEarlyMap(state, pt, vmAllocHead, MmuFlag::Write);
                vmAllocHead += PageSize();
            }
        }
        Log("Temp mappings prepared: 0x%tx (access @ %p, %zu)", LogLevel::Info,
            tempMapBase, tempMapAccess.Begin(), tempMapAccess.Size());

        return vmAllocHead;
    }

    void ArchEarlyMap(InitState& state, Paddr paddr, uintptr_t vaddr, MmuFlags flags)
    {
        DoEarlyMap(state, paddr, vaddr, flags);
    }

    void* ArchSetTempMap(KernelMap* map, size_t index, Paddr paddr)
    {
        NPK_ASSERT(map != nullptr);
        NPK_ASSERT(index < tempMapAccess.Size());

        const uintptr_t vaddr = (index << PfnShift()) + tempMapBase;

        const uint64_t pte = (paddr & addrMask) | PresentFlag | WriteFlag;
        SET_PTE(&tempMapAccess[index], pte);
        INVLPG(vaddr);

        return reinterpret_cast<void*>(vaddr);
    }

    struct WalkResult
    {
        PageAccessRef ptRef;
        size_t level;
        uint64_t* pte;
        bool complete;
        bool bad;
    };

    static WalkResult WalkTables(KernelMap* map, sl::Span<size_t> indices)
    {
        WalkResult result {};

        PageAccessRef nextPtRef = AccessPage(*map);
        for (size_t i = ptLevels; i > 0; i--)
        {
            if (!nextPtRef.Valid())
            {
                result.bad = true;
                return result;
            }

            result.pte = &static_cast<PageTable*>(nextPtRef->value)->ptes[indices[i]];
            result.level = i;
            result.ptRef = nextPtRef;

            if ((*result.pte & PresentFlag) == 0)
            {
                result.complete = false;
                return result;
            }

            const uint64_t nextPt = *result.pte & addrMask;
            nextPtRef = AccessPage(nextPt);
        }

        result.complete = *result.pte & PresentFlag;
        return result;
    }

    MmuError ArchAddMap(KernelMap* map, uintptr_t vaddr, Paddr paddr, MmuFlags flags)
    {
        NPK_CHECK(map != nullptr, MmuError::InvalidArg);

        const uint64_t intermediateFlags = PresentFlag | WriteFlag | 
            (flags.Has(MmuFlag::User) ? UserFlag : 0);

        size_t allocPageCount = 0;
        PageInfo* allocPages[MaxPtIndices];

        size_t indices[MaxPtIndices];
        GetAddressIndices(vaddr, indices);

        WalkResult path = WalkTables(map, indices);
        NPK_CHECK(!path.bad, MmuError::InvalidArg);
        NPK_CHECK(!path.complete, MmuError::MapAlreadyExits);

        //allocate pages up front, so we know we have enough before make them
        //visible to the mmu
        while (allocPageCount < path.level - 1)
        {
            allocPages[allocPageCount++] = AllocPage(true);
            if (allocPages[allocPageCount - 1] == nullptr)
            {
                //an allocation failed, free pages and abort
                for (size_t i = 0; i < allocPageCount; i++)
                    FreePage(allocPages[i]);
                return MmuError::PageAllocFailed;
            }
        }

        while (path.level != 1)
        {
            auto page = allocPages[path.level - 1];
            page->mmu.validPtes = 0;

            LookupPageInfo(path.ptRef->key)->mmu.validPtes++;
            SET_PTE(path.pte, LookupPagePaddr(page) | intermediateFlags);

            path.level--;
            path.ptRef = AccessPage(page);
            NPK_ASSERT(path.ptRef.Valid());
            path.pte = &static_cast<PageTable*>(path.ptRef->value)->ptes[indices[path.level]];
        }

        uint64_t pte = (paddr & addrMask) | PresentFlag;
        ApplyFlagsToPte(pte, flags);
        SET_PTE(path.pte, pte);

        return MmuError::Success;
    }
}
