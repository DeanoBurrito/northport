#include <private/Core.hpp>
#include <lib/Memory.hpp>
#include <lib/Maths.hpp>

namespace Npk
{
    CPU_LOCAL(PageAccessCache, static accessCache);

    bool Private::PmaCacheSetEntry(size_t slot, void** curVaddr, 
        Paddr curPaddr, Paddr nextPaddr)
    {
        (void)curPaddr;

        *curVaddr = HwSetTempMapSlot(slot, nextPaddr);

        return true;
    }

    static const HwDirectMapSegment* GetDirectMapSegment(Paddr paddr)
    {
        const auto segments = HwGetDirectMapSegments();

        for (size_t i = 0; i < segments.Size(); i++)
        {
            const auto& seg = segments[i];

            if (paddr >= seg.physBase && paddr < seg.physBase + seg.length)
                return &seg;
        }

        return nullptr;
    }

    size_t CopyFromPhysical(Paddr base, sl::Span<char> buffer)
    {
        size_t copied = 0;

        while (copied < buffer.Size())
        {
            const Paddr paddr = base + copied;
            const size_t remaining = buffer.Size() - copied;

            auto* seg = GetDirectMapSegment(paddr);
            if (seg != nullptr)
            {
                const size_t offset = paddr - seg->physBase;
                const size_t runLen = sl::Min(remaining, seg->length - offset);
                auto src = reinterpret_cast<const void*>(
                    seg->virtBase + offset);

                sl::MemCopy(&buffer[copied], src, runLen);
                copied += runLen;

                continue;
            }

            const auto prevIpl = EnsureIpl(Ipl::Dpc);
            const size_t offset = paddr & PageMask();
            const size_t runLen = sl::Min(remaining, PageSize() - offset);
            do
            {
                PageAccessRef access = AccessPage(AlignDownPage(paddr));
                if (!access.Valid())
                    return copied;

                const auto src = reinterpret_cast<void*>(
                    reinterpret_cast<uintptr_t>(access.vaddr) + offset);

                sl::MemCopy(&buffer[copied], src, runLen);
            }
            while (false);

            if (prevIpl < Ipl::Dpc)
                LowerIpl(prevIpl);
            copied += runLen;
        }

        return buffer.Size();
    }

    void Private::InitPageAccessCache(uintptr_t slotsBase, size_t slotsCount)
    {
        auto* ptr = reinterpret_cast<PageAccessCache::Slot*>(slotsBase);
        accessCache->Init({ ptr, slotsCount }, 0);

        Log("Page access initialized: slots=0x%tx (x%zu)", LogLevel::Trace,
            slotsBase, slotsCount);
    }

    PageAccessRef AccessPage(Paddr paddr)
    {
        NPK_CHECK((paddr & PageMask()) == 0, {});
        NPK_CHECK(CurrentIpl() >= Ipl::Dpc, {});

        const HwDirectMapSegment* seg = GetDirectMapSegment(paddr);
        if (seg != nullptr)
        {
            uintptr_t value = paddr - seg->physBase;
            value += seg->virtBase;

            PageAccessRef ref {};
            ref.vaddr = reinterpret_cast<void*>(value);
            ref.paddr = paddr;

            return ref;
        }

        auto slot = accessCache->Get(paddr);
        if (!slot.Valid())
            return {};

        auto vaddr = slot->value;
        PageAccessRef ref(sl::Move(slot));
        ref.paddr = paddr;
        ref.vaddr = vaddr;

        return ref;
    }

    void DestroyPageAccess(PageAccessRef* ref)
    {
        NPK_CHECK(ref != nullptr, );
        NPK_ASSERT(CurrentIpl() >= Ipl::Dpc);

        PageAccessRef other = sl::Move(*ref);

        ref->vaddr = nullptr;
        ref->paddr = {};

        //other goes out of scope, dead.
    }
}
