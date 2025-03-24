#pragma once

#include <core/Defs.h>

namespace Npk
{
    sl::Opt<PageInfo*> PmAlloc(MemoryDomain& dom); //NOTE: returns zeroed pages
    void PmFree(MemoryDomain& dom, PageInfo* info);
    
    SL_ALWAYS_INLINE
    PageInfo* PmLookup(const MemoryDomain& dom, Paddr addr)
    {
        return dom.infoDb + ((addr - dom.physOffset) >> PfnShift());
    }

    SL_ALWAYS_INLINE
    Paddr PmRevLookup(const MemoryDomain& dom, PageInfo* info)
    {
        return ((info - dom.infoDb) << PfnShift()) + dom.physOffset;
    }
}
