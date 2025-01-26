#pragma once

#include <arch/Hat.h>
#include <core/Pmm.h>
#include <Span.h>

namespace Npk::Services
{
    struct VmObject;

    struct SwapKey;
    
    struct SwapBackend
    {
        bool (*Reserve)(size_t length, SwapKey* key);
        void (*Unreserve)(SwapKey key, size_t length);
        bool (*Read)(SwapKey key, size_t offset, uintptr_t paddr);
        bool (*Write)(SwapKey key, size_t offset, uintptr_t paddr);
    };

    void InitSwap();

    sl::Opt<SwapKey> ReserveSwap(size_t length);
    void UnreserveSwap(SwapKey key, size_t length);
    bool SwapOut(SwapKey key, size_t offset, uintptr_t paddr);
    bool SwapIn(SwapKey key, size_t offset, uintptr_t paddr);

    VmObject* CreateMmioVmo(uintptr_t paddr, size_t length, HatFlags hatFlags);
    bool DestroyMmioVmo(VmObject* vmo, bool force);

    sl::Opt<uintptr_t> GetMmioVmoPage(VmObject* vmo, size_t offset);
    HatFlags GetMmioVmoHatFlags(VmObject* vmo, size_t offset);
}
