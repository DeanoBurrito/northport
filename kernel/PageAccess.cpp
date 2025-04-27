#include <KernelApi.hpp>
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

    size_t CopyFromPages(Paddr base, sl::Span<char> buffer)
    {
        for (size_t i = 0; i < buffer.Size(); i += PageSize())
        {
            PageAccessRef access = AccessPage(base + i);
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
        return accessCache.Get(paddr);
    }
}
