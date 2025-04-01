#pragma once

#include <Types.h>
#include <Compiler.h>

namespace Npk
{
    uintptr_t ArchMyCpuLocals();

    SL_ALWAYS_INLINE
    void WaitForIntr();

    SL_ALWAYS_INLINE
    bool IntrsExchange(bool on);

    SL_ALWAYS_INLINE
    bool IntrsOff()
    {
        return IntrsExchange(false);
    }

    SL_ALWAYS_INLINE
    bool IntrsOn()
    {
        return IntrsExchange(true);
    }

    void ArchInitEarly();
}

#ifdef __x86_64__
    #include <hardware/x86_64/Arch.hpp>
#else
    #error "Unsupported target architecture."
#endif
