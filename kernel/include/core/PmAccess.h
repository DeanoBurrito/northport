#pragma once

#include <containers/LruCache.h>
#include <core/PmAlloc.h>

namespace Npk::Core
{
    bool PmaCacheSetEntry(size_t slot, void** currVaddr, Paddr currPaddr, Paddr newPaddr);

    using PmaCache = sl::LruCache<Paddr, void*, PmaCacheSetEntry>;
    using PmaRef = PmaCache::CacheRef;

    void InitPmaCache(sl::Span<PmaCache::Slot> slots, Paddr defaultValue);
    PmaRef GetPmAccess(PageInfo* page);
    PmaRef GetPmAccess(Paddr paddr);
    size_t CopyFromPm(Paddr paddr, sl::Span<char> buffer);
}
