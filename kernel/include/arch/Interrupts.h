#pragma once

#include <arch/__Select.h>
#include <Optional.h>

namespace Npk
{
    struct MsiConfig
    {
        uintptr_t address;
        uintptr_t data;
    };

    bool InterruptsEnabled();
    void EnableInterrupts();
    void DisableInterrupts();

    bool SendIpi(size_t dest, bool urgent);
    bool RoutePinInterrupt(size_t pin, size_t core, size_t vector);

    void Wfi();
    sl::Opt<MsiConfig> ConstructMsi(size_t core, size_t vector);
    bool DeconstructMsi(MsiConfig cfg, size_t& core, size_t& vector);

    [[noreturn]]
    inline void Halt()
    { 
        while (true) 
            Wfi(); 
        __builtin_unreachable();
    }
}

#ifdef NPK_ARCH_INCLUDE_INTERRUPTS
#include NPK_ARCH_INCLUDE_INTERRUPTS
#endif
