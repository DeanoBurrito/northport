#include <hardware/Arch.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <hardware/x86_64/Mmu.hpp>
#include <KernelApi.hpp>
#include <Memory.h>

extern "C"
{
    extern char SpinupBlob[];
    extern char _EndOfSpinupBlob[];
}

#define INVLPG(vaddr) do { asm volatile("invlpg (%0)" :: "r"(vaddr) : "memory"); } while (false)
#define SET_PTE(pte_ptr, value) do { asm volatile("mov %0, (%1)" :: "r"(value), "r"(pte_ptr)); } while (false)

namespace Npk
{
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

    PageTable* kernelMap;
    Paddr apBootPage;

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

    uintptr_t ArchInitBspMmu(InitState& state)
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

        const Paddr ptRoot = state.PmAlloc();
        kernelMap = reinterpret_cast<PageTable*>(ptRoot);
        //TODO: map hhdm as pageaccess optimization

        apBootPage = state.PmAlloc();
        ArchEarlyMap(state, apBootPage, apBootPage, MmuFlag::Write | MmuFlag::Fetch);

        const size_t blobLength = (uintptr_t)_EndOfSpinupBlob - (uintptr_t)SpinupBlob;
        NPK_ASSERT(blobLength <= PageSize());
        sl::MemCopy(reinterpret_cast<void*>(apBootPage + state.dmBase), SpinupBlob, blobLength);
        Log("AP boot blob @ 0x%tx", LogLevel::Verbose, apBootPage);


        return -(1ull << (9 * ptLevels + 11));
    }

    void ArchEarlyMap(InitState& state, Paddr paddr, uintptr_t vaddr, MmuFlags flags)
    {
        size_t indices[MaxPtIndices];
        GetAddressIndices(vaddr, indices);

        auto pt = reinterpret_cast<PageTable*>(reinterpret_cast<uintptr_t>(kernelMap) + state.dmBase);
        for (size_t i = ptLevels; i != 1; i--)
        {
            uint64_t* pte = &pt->ptes[indices[i]];
            if ((*pte & PresentFlag) == 0)
                SET_PTE(pte, state.PmAlloc() | PresentFlag | WriteFlag);

            pt = reinterpret_cast<PageTable*>((*pte & addrMask) + state.dmBase);
        }

        uint64_t pte = (paddr & addrMask) | PresentFlag;
        if (vaddr >= (static_cast<uintptr_t>(~0) >> 1))
            pte |= GlobalFlag; //only set global flag for higher-half mappings

        ApplyFlagsToPte(pte, flags);
        SET_PTE(&pt->ptes[indices[1]], pte);
    }
}
