#include <CoreApi.hpp>
#include <Memory.h>
#include <Maths.h>

namespace Npk
{
    static PageAccessCache accessCache;

    bool Internal::PmaCacheSetEntry(size_t slot, void** curVaddr, Paddr curPaddr, Paddr nextPaddr)
    {
        (void)curPaddr;

        *curVaddr = ArchSetTempMap(MyKernelMap(), slot, nextPaddr);
        return true;
    }

    size_t CopyFromPhysical(Paddr base, sl::Span<char> buffer)
    {
        const size_t offset = base & PageMask();

        size_t i = 0;
        if (offset != 0)
        {
            PageAccessRef access = AccessPage(AlignDownPage(base));
            if (!access.Valid())
                return 0;

            const size_t copyLen = sl::Min(buffer.Size(), PageSize() - offset);
            const uintptr_t srcAddr = reinterpret_cast<uintptr_t>(access->value);
            sl::MemCopy(&buffer[0], reinterpret_cast<void*>(srcAddr + offset), copyLen);

            i += copyLen;
            base = AlignUpPage(base);
        }

        for (; i < buffer.Size(); i += PageSize())
        {
            PageAccessRef access = AccessPage(base + i - offset);
            if (!access.Valid())
                return i;

            const size_t copyLen = sl::Min(buffer.Size() - i, PageSize());
            sl::MemCopy(&buffer[i], access->value, copyLen);
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

        return accessCache.Get(paddr);
    }
}
