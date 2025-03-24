#include <core/PmAccess.h>
#include <core/Log.h>
#include <Memory.h>

namespace Npk::Core
{
    sl::RunLevelLock<RunLevel::Dpc> pmaCacheLock;
    PmaCache pmaCache;

    bool PmaCacheSetEntry(size_t slot, void** currVaddr, Paddr currPaddr, Paddr newPaddr)
    {
        //TODO: support for arch-specific mechanism, like a direct map

        //NOTE: this is only called from PmaCache::Get(), which already holds the pmaLock
        auto& dom = LocalDomain();
        Log("PMA recache: 0x%tx switched to 0x%tx, vaddr %p", LogLevel::Debug, currPaddr, newPaddr, *currVaddr);

        *currVaddr = reinterpret_cast<void*>(dom.pmaBase + (slot << PfnShift()));
        if (MmuUnmap(dom.kernelSpace, *currVaddr).HasError())
            return false;

        const MmuFlags flags = MmuFlag::Global | MmuFlag::Write;
        if (MmuMap(dom.kernelSpace, *currVaddr, newPaddr, flags) != MmuError::Success)
            return false;

        return true;
    }

    void InitPmaCache(sl::Span<PmaCache::Slot> slots, Paddr defaultValue)
    {
        pmaCache.Init(slots, defaultValue);
    }

    PmaRef GetPmAccess(PageInfo* page)
    {
        sl::ScopedLock scopeLock(pmaCacheLock);
        return pmaCache.Get(PmRevLookup(LocalDomain(), page));
    }

    PmaRef GetPmAccess(Paddr paddr)
    {
        sl::ScopedLock scopeLock(pmaCacheLock);
        return pmaCache.Get(paddr);
    }

    size_t CopyFromPm(Paddr paddr, sl::Span<char> buffer)
    {
        size_t copied = 0;
        while (copied < buffer.Size())
        {
            const size_t offset = (paddr + copied) & PageMask();
            auto pma = GetPmAccess(paddr + copied - offset);
            if (!pma.Valid())
                return copied;

            const size_t copyOffset = copied == 0 ? offset : 0;
            sl::MemCopy(&buffer[copied], 
                &static_cast<char*>(pma->value)[copyOffset], PageSize() - copyOffset);
            copied += PageSize() - copyOffset;
        }

        return copied;
    }
}
