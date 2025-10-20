#include <VmPrivate.hpp>
#include <Core.hpp>

namespace Npk::Private
{
    MmuFlags VmToMmuFlags(VmFlags flags, MmuFlags extra)
    {
        MmuFlags outFlags = extra;

        if (flags.Has(VmFlag::Write))
            outFlags.Set(MmuFlag::Write);
        if (flags.Has(VmFlag::Fetch))
            outFlags.Set(MmuFlag::Fetch);

        return outFlags;
    }

    sl::Opt<Paddr> AllocatePageTable(size_t level)
    {
        const size_t ptSize = HwGetPageTableSize(level);

        if (ptSize == PageSize())
        {
            auto page = AllocPage(true);
            if (page == nullptr)
                return {};

            return LookupPagePaddr(page);
        }

        //TODO: implement me!
        NPK_UNREACHABLE();
    }

    void FreePageTable(size_t level, Paddr paddr)
    {
        const size_t ptSize = HwGetPageTableSize(level);
        
        if (ptSize == PageSize())
        {
            auto info = LookupPageInfo(paddr);
            FreePage(info);
            return;
        }

        //TODO:
        NPK_UNREACHABLE();
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
            const auto nextPt = AllocatePageTable(result.level - 1);
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

        const auto mmuFlags = VmToMmuFlags(flags, {});

        HwPte pte {};
        HwPteValid(&pte, true);
        HwPteAddr(&pte, paddr);
        HwPteFlags(&pte, mmuFlags);

        HwCopyPte(result.pte, &pte);
        
        return VmStatus::Success;
    }

    VmStatus ClearMap(HwMap map, uintptr_t vaddr)
    {
        MmuWalkResult result {};
        PageAccessRef ptRef {};

        if (!HwWalkMap(map, vaddr, result, &ptRef))
            return VmStatus::InternalError;

        if (!result.complete)
            return VmStatus::BadVaddr;

        HwPte pte {};
        HwPteValid(&pte, false);
        HwCopyPte(result.pte, &pte);
        //TODO: update valid PTE count in PT metadata
        //TODO: invalidate here or leave for higher level functions?

        return VmStatus::Success;
    }
}
