#include <HardwarePrivate.hpp>
#include <hardware/x86_64/Private.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <Core.hpp>
#include <EntryPrivate.hpp>
#include <Memory.hpp>
#include <Maths.hpp>

#define INVLPG(vaddr) \
    do \
    { \
        asm volatile("invlpg (%0)" :: "r"(vaddr) : "memory"); \
    } while (false)

#define COPY_PTE(dest_ptr, src_ptr) \
    do \
    { \
        asm volatile("movq (%1), %%rcx; movq %%rcx, (%0)" :: \
            "r"(dest_ptr), "r"(src_ptr) : "memory"); \
    } while (false)

namespace Npk
{
    constexpr size_t PtEntries = 512;

    constexpr uint64_t PresentFlag = 1 << 0;
    constexpr uint64_t WriteFlag = 1 << 1;
    constexpr uint64_t UserFlag = 1 << 2;
    constexpr uint64_t NxFlag = 1ul << 63;
    constexpr uint64_t PatBitsMask = (1 << 7) | (1 << 4) | (1 << 3);
    constexpr uint64_t PatUcFlag = (1 << 4) | (1 << 3); //(3) for UC
    constexpr uint64_t PatWcFlag = (1 << 7) | (1 << 3); //(5) for WC

    struct PageTable
    {
        HwPte ptes[PtEntries];
    };

    static uint64_t addrMask;
    static size_t ptLevels;
    static bool nxSupported;
    static bool patSupported;

    uintptr_t tempMapBase;
    sl::Span<uint64_t> tempMapAccess;

    Paddr kernelMap;
    Paddr apBootPage;

    static Paddr DoEarlyMap(InitState& state, Paddr paddr, uintptr_t vaddr,
        MmuFlags flags)
    {
        size_t indices[6];
        indices[5] = (vaddr >> 48) & 0x1FF;
        indices[4] = (vaddr >> 39) & 0x1FF;
        indices[3] = (vaddr >> 30) & 0x1FF;
        indices[2] = (vaddr >> 21) & 0x1FF;
        indices[1] = (vaddr >> 12) & 0x1FF;
        indices[0] = 0;

        auto pt = reinterpret_cast<PageTable*>(kernelMap + state.dmBase);
        for (size_t i = ptLevels; i != 1; i--)
        {
            auto pte = &pt->ptes[indices[i]];
            if ((pte->value & PresentFlag) == 0)
            {
                HwPte localPte {};
                localPte.value = state.PmAlloc() | PresentFlag | WriteFlag;
                COPY_PTE(&pte->value, &localPte);
            }

            pt = reinterpret_cast<PageTable*>((pte->value & addrMask)
                + state.dmBase);
        }

        HwPte pte {};
        HwPteFlags(&pte, flags);
        pte.value |= (paddr & addrMask) | PresentFlag;
        COPY_PTE(&pt->ptes[indices[1]].value, &pte.value);

        return reinterpret_cast<Paddr>(pt) - state.dmBase;
    }

    uintptr_t HwInitBspMmu(InitState& state, size_t tempMapCount)
    {
        const uint64_t cr4 = READ_CR(4);
        ptLevels = (cr4 & (1 << 1)) ? 5 : 4;

        nxSupported = CpuHasFeature(CpuFeature::NoExecute);
        patSupported = CpuHasFeature(CpuFeature::Pat);

        if (!patSupported)
            Log("PAT not supported on this cpu.", LogLevel::Warning);
        //TODO: support non-PAT systems properly

        addrMask = 1ull << (9 * ptLevels + 12);
        addrMask--;
        addrMask &= ~0xFFFul;

        kernelMap = state.PmAlloc();
        domain0.kernelSpace = kernelMap;
        //TODO: software direct map as PageAccess optimization

        apBootPage = state.PmAlloc();
        HwEarlyMap(state, apBootPage, apBootPage, 
            MmuFlag::Write | MmuFlag::Fetch);
        const size_t blobLength = 
            (uintptr_t)_EndOfSpinupBlob - (uintptr_t)SpinupBlob;
        NPK_ASSERT(blobLength <= PageSize());

        sl::MemCopy(reinterpret_cast<void*>(apBootPage + state.dmBase), 
            SpinupBlob, blobLength);
        Log("AP boot blob @ 0x%tx", LogLevel::Verbose, apBootPage);

        uintptr_t vmAllocHead = -(1ull << (9 * ptLevels + 11)); 

        tempMapBase = vmAllocHead;
        tempMapCount = sl::AlignUp(tempMapCount, PtEntries);
        vmAllocHead += tempMapCount << PfnShift();
        tempMapAccess = { reinterpret_cast<uint64_t*>(vmAllocHead), tempMapCount };

        for (size_t i = 0; i < tempMapCount; i++)
        {
            const Paddr pt = DoEarlyMap(state, 0, 
                tempMapBase + (i << PfnShift()), MmuFlag::Write);

            if ((i & (PtEntries - 1)) == 0)
            {
                HwEarlyMap(state, pt, vmAllocHead, MmuFlag::Write);
                vmAllocHead += PageSize();
            }
        }
        Log("Temp mappings prepared: 0x%tx (access @ %p, %zu)", LogLevel::Info,
            tempMapBase, tempMapAccess.Begin(), tempMapAccess.Size());

        return vmAllocHead;
    }

    void HwEarlyMap(InitState& state, Paddr paddr, uintptr_t vaddr,
        MmuFlags flags)
    {
        DoEarlyMap(state, paddr, vaddr, flags);
    }
    
    void* HwSetTempMapSlot(size_t index, Paddr paddr)
    {
        if (index >= tempMapAccess.Size())
            return nullptr;

        const uintptr_t vaddr = (index << PfnShift()) + tempMapBase;
        const uint64_t pte = (paddr & addrMask) | PresentFlag | WriteFlag;

        COPY_PTE(&tempMapAccess[index], &pte);
        INVLPG(vaddr);

        return reinterpret_cast<void*>(vaddr);
    }

    void HwFlushTlb(uintptr_t base, size_t length)
    {
        const uintptr_t top = base + length;
        base = AlignDownPage(base);

        while (base < top)
        {
            INVLPG(base);
            base += PageSize();
        }
    }

    HwMap HwKernelMap(sl::Opt<HwMap> next)
    {
        const Paddr prev = READ_CR(3);

        if (next.HasValue())
            WRITE_CR(3, *next);
        else
            WRITE_CR(3, kernelMap);

        return prev;
    }

    HwMap HwUserMap(sl::Opt<HwMap> next)
    {
        const Paddr prev = READ_CR(3);

        //TODO: ensure higher half is in sync: we should just map 
        //pml4[256-511]in all addr spaces and then clone them.
        if (next.HasValue())
            WRITE_CR(3, *next);

        return prev;
    }

    bool HwWalkMap(HwMap root, uintptr_t vaddr, MmuWalkResult& result, 
        void* ptRef)
    {
        PageAccessRef ref = AccessPage(root);
        result.level = ptLevels - 1;
        result.complete = false;
        result.pte = nullptr;

        if (HwContinueWalk(root, vaddr, result, &ref))
        {
            *static_cast<PageAccessRef*>(ptRef) = ref;
            return true;
        }

        return false;
    }

    bool HwContinueWalk(HwMap root, uintptr_t vaddr, MmuWalkResult& result, 
        void* ptRef)
    {
        (void)root;

        if (result.complete || ptRef == nullptr)
            return false;

        size_t indices[6];
        indices[5] = (vaddr >> 48) & 0x1FF;
        indices[4] = (vaddr >> 39) & 0x1FF;
        indices[3] = (vaddr >> 30) & 0x1FF;
        indices[2] = (vaddr >> 21) & 0x1FF;
        indices[1] = (vaddr >> 12) & 0x1FF;
        indices[0] = 0;

        size_t level = result.level + 1;
        PageAccessRef nextPtRef = *static_cast<PageAccessRef*>(ptRef);
        PageAccessRef localPtRef {};
        HwPte* pte = result.pte;

        while (level != 0)
        {
            if (!nextPtRef.Valid())
                return false;

            auto pt = static_cast<PageTable*>(nextPtRef->value);
            pte = &pt->ptes[indices[level]];
            level--;
            localPtRef = nextPtRef;

            if (level == 0)
                break;

            if ((pte->value & PresentFlag) == 0)
            {
                result.complete = false;
                result.level = level;
                result.pte = pte;
                *static_cast<PageAccessRef*>(ptRef) = localPtRef;

                return true;
            }

            const Paddr nextPt = pte->value & addrMask;
            nextPtRef = AccessPage(nextPt);
        }

        if (level == result.level + 1)
            return false;

        result.complete = pte->value & PresentFlag;
        result.pte = pte;
        result.level = level;
        *static_cast<PageAccessRef*>(ptRef) = localPtRef;

        return true;
    }

    bool HwIntermediatePte(HwPte* pte, sl::Opt<Paddr> next, bool valid)
    {
        HwPte localPte {};

        if (valid)
            localPte.value |= PresentFlag | WriteFlag;
        if (next.HasValue())
            localPte.value |= *next;

        COPY_PTE(pte, &localPte);

        return true;
    }

    bool HwPteValid(HwPte* pte, sl::Opt<bool> set)
    {
        if (pte == nullptr)
            return false;

        const bool prev = pte->value & PresentFlag;

        if (set.HasValue())
        {
            HwPte localPte;

            COPY_PTE(&localPte, pte);
            localPte.value |= PresentFlag;
            COPY_PTE(pte, &localPte);
        }

        return prev;
    }

    MmuFlags HwPteFlags(HwPte* pte, sl::Opt<MmuFlags> set)
    {
        if (pte == nullptr)
            return {};

        MmuFlags current {};
        if (pte->value & WriteFlag)
            current |= MmuFlag::Write;
        if (pte->value & UserFlag)
            current |= MmuFlag::User;
        if (!nxSupported || ((pte->value & NxFlag) == 0))
            current |= MmuFlag::Fetch;
        if (patSupported && ((pte->value & PatUcFlag) == PatUcFlag))
            current |= MmuFlag::Mmio;
        if (patSupported && ((pte->value & PatWcFlag) == PatWcFlag))
            current |= MmuFlag::Framebuffer;

        if (set.HasValue())
        {
            pte->value &= ~WriteFlag;
            if (set->Has(MmuFlag::Write))
                pte->value |= WriteFlag;

            pte->value &= ~UserFlag;
            if (set->Has(MmuFlag::User))
                pte->value |= UserFlag;

            if (nxSupported)
            {
                pte->value &= ~NxFlag;
                if (!set->Has(MmuFlag::Fetch))
                    pte->value |= NxFlag;
            }

            if (patSupported)
            {
                pte->value &= ~PatBitsMask;
                if (set->Has(MmuFlag::Framebuffer))
                    pte->value |= PatWcFlag;
                else if (set->Has(MmuFlag::Mmio))
                    pte->value |= PatUcFlag;
            }
        }

        return current;
    }

    Paddr HwPteAddr(HwPte* pte, sl::Opt<Paddr> set)
    {
        if (pte == nullptr)
            return 0;

        const Paddr prev = pte->value & addrMask;

        if (set.HasValue())
        {
            HwPte localPte;

            COPY_PTE(&localPte, pte);
            localPte.value = localPte.value & ~addrMask;
            localPte.value |= *set;
            COPY_PTE(pte, &localPte);
        }

        return prev;
    }

    void HwCopyPte(HwPte* dest, const HwPte* src)
    {
        COPY_PTE(dest, src);
    }

    size_t HwGetPageTableSize(size_t level)
    {
        (void)level;

        return 0x1000;
    }
}
