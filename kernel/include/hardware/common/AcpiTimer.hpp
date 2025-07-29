#pragma once

#include <Types.h>
#include <Compiler.h>

namespace Npk
{
    bool InitAcpiTimer(uintptr_t& vaddrBase);

    bool AcpiTimerAvailable();
    bool AcpiTimerIs32Bit();
    uint32_t AcpiTimerRead();

    SL_ALWAYS_INLINE
    uint64_t AcpiTimerFrequency()
    {
        //taken directly from the acpi spec, about as magic as it gets :sparkle:
        return 3579545;
    }
}
