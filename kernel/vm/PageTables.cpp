#include <VmPrivate.hpp>
#include <Core.hpp>

namespace Npk
{
    MmuFlags Private::VmToMmuFlags(VmFlags flags, MmuFlags extra)
    {
        MmuFlags outFlags = extra;

        if (flags.Has(VmFlag::Write))
            outFlags.Set(MmuFlag::Write);
        if (flags.Has(VmFlag::Fetch))
            outFlags.Set(MmuFlag::Fetch);

        return outFlags;
    }

    sl::Opt<Paddr> Private::AllocatePageTable(size_t level)
    {
        const size_t ptSize = HwGetPageTableSize(level);

        if (ptSize == PageSize())
        {
            auto page = AllocPage(true);
            if (page == nullptr)
                return {};

            return LookupPagePaddr(page);
        }

        Panic("Non-page sized page tables not currently implemented", nullptr);
    }

    void Private::FreePageTable(size_t level, Paddr paddr)
    {
        const size_t ptSize = HwGetPageTableSize(level);
        
        if (ptSize == PageSize())
        {
            auto info = LookupPageInfo(paddr);
            FreePage(info);
            return;
        }

        Panic("Non-page sized page tables not currently implemented", nullptr);
    }

    VmStatus PrimeMapping(HwMap map, uintptr_t vaddr, MmuWalkResult& resultOut,
        PageAccessRef& ptRefOut)
    {
        PageAccessRef ptRef;
        MmuWalkResult result {};

        if (!HwWalkMap(map, vaddr, result, &ptRef))
            return VmStatus::InternalError;

        if (result.complete)
            return VmStatus::AlreadyMapped;

        while (result.level != 0)
        {
            const auto nextPt = Private::AllocatePageTable(result.level - 1);
            if (!nextPt.HasValue())
                return VmStatus::Shortage;

            if (!HwIntermediatePte(result.pte, *nextPt, true))
                return VmStatus::InternalError;

            if (!HwContinueWalk(map, vaddr, result, &ptRef))
                return VmStatus::InternalError;
        }

        resultOut = result;
        ptRefOut = ptRef;
        return VmStatus::Success;
    }

    VmStatus SetMap(HwMap map, uintptr_t vaddr, Paddr paddr, VmFlags flags)
    {
        MmuWalkResult result {};
        PageAccessRef ptRef {};

        auto status = PrimeMapping(map, vaddr, result, ptRef);
        if (status != VmStatus::Success)
            return status;

        if (result.complete)
            return VmStatus::AlreadyMapped;

        const auto mmuFlags = Private::VmToMmuFlags(flags, {});

        HwPte pte {};
        HwPteValid(&pte, true);
        HwPteAddr(&pte, paddr);
        HwPteFlags(&pte, mmuFlags);

        HwCopyPte(result.pte, &pte);
        
        return VmStatus::Success;
    }

    VmStatus ClearMap(HwMap map, uintptr_t vaddr, Paddr* paddr)
    {
        MmuWalkResult result {};
        PageAccessRef ptRef {};

        if (!HwWalkMap(map, vaddr, result, &ptRef))
            return VmStatus::InternalError;

        if (!result.complete)
            return VmStatus::BadVaddr;

        HwPte pte {};
        HwCopyPte(&pte, result.pte);
        HwPteValid(&pte, false);
        HwCopyPte(result.pte, &pte);

        Paddr ptePaddr = HwPteAddr(result.pte, {});
        if (paddr != nullptr)
            *paddr = ptePaddr;

        auto info = LookupPageInfo(ptePaddr);
        info->mmu.validPtes--;

        if (info->mmu.validPtes == 0)
            Log("Leaking empty PT page, TODO!", LogLevel::Error);

        return VmStatus::Success;
    }

    VmStatus PrimeKernelMap(uintptr_t vaddr)
    {
        MmuWalkResult result;
        PageAccessRef ref;

        return PrimeMapping(MyKernelMap(), vaddr, result, ref);
    }

    VmStatus SetKernelMap(uintptr_t vaddr, Paddr paddr, VmFlags flags)
    {
        return SetMap(MyKernelMap(), vaddr, paddr, flags);
    }

    VmStatus ClearKernelMap(uintptr_t vaddr, Paddr* paddr)
    {
        return ClearMap(MyKernelMap(), vaddr, paddr);
    }
}
