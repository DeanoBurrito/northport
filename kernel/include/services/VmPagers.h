#pragma once

#include <arch/Hat.h>
#include <Types.h>
#include <Optional.h>
#include <Span.h>

namespace Npk::Services
{
    struct VmObject;

    VmObject* CreateMmioVmo(uintptr_t paddr, size_t length, HatFlags hatFlags);
    bool DestroyMmioVmo(VmObject* vmo, bool force);

    sl::Opt<uintptr_t> GetMmioVmoPage(VmObject* vmo, size_t offset);
    HatFlags GetMmioVmoHatFlags(VmObject* vmo, size_t offset);
}
