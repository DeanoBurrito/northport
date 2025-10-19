#include <EntryPrivate.hpp>
#include <Maths.hpp>
#include <Memory.hpp>

namespace Npk
{
    char* InitState::VmAlloc(size_t length)
    {
        const uintptr_t ret = vmAllocHead;
        vmAllocHead += AlignUpPage(length);

        return reinterpret_cast<char*>(ret);
    }

    char* InitState::VmAllocAnon(size_t length)
    {
        char* base = VmAlloc(length);
        for (size_t i = 0; i < length; i += PageSize())
            HwEarlyMap(*this, PmAlloc(), (uintptr_t)base + i, MmuFlag::Write);

        return base;
    }

    Paddr InitState::PmAlloc()
    {
        Loader::MemoryRange range;

        while (true)
        {
            const size_t count = Loader::GetUsableRanges(
                { &range, 1 }, pmAllocIndex);

            if (count == 0)
                EarlyPanic("No usable PM ranges");

            pmAllocHead = sl::Max(pmAllocHead, range.base);
            if (pmAllocHead + PageSize() > range.base + range.length)
            {
                pmAllocIndex++;
                continue;
            }

            usedPages++;
            const Paddr ret = pmAllocHead;
            pmAllocHead += PageSize();

            sl::MemSet(reinterpret_cast<void*>(dmBase + ret), 0, PageSize());
            return ret;
        }
        NPK_EARLY_UNREACHABLE();
    }
}
