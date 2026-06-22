#include <Core.hpp>
#include <lib/Memory.hpp>
#include <lib/Maths.hpp>

namespace Npk
{
    static PageAccessCache accessCache;

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

            const size_t offset = paddr & PageMask();
            PageAccessRef access = AccessPage(AlignDownPage(paddr));
            if (!access.Valid())
                return copied;

            const size_t runLen = sl::Min(remaining, PageSize() - offset);
            const auto src = reinterpret_cast<void*>(
                reinterpret_cast<uintptr_t>(access.vaddr) + offset);

            sl::MemCopy(&buffer[copied], src, runLen);
            copied += runLen;
        }

        return buffer.Size();
    }

    void InitPageAccessCache(size_t entries, uintptr_t slots)
    {
        auto slotsPtr = reinterpret_cast<PageAccessCache::Slot*>(slots);
        accessCache.Init({ slotsPtr, entries }, 0);

        Log("Initialized page access cache", LogLevel::Trace);
    }

    PageAccessRef AccessPage(Paddr paddr)
    {
        NPK_CHECK((paddr & PageMask()) == 0, {});

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

        auto slot = accessCache.Get(paddr);
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

        PageAccessRef other = sl::Move(*ref);

        ref->vaddr = nullptr;
        ref->paddr = {};

        //other goes out of scope, dead.
    }
}
