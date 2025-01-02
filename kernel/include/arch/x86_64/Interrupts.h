#pragma once

#include <Types.h>
#include <Compiler.h>

namespace Npk
{
    SL_ALWAYS_INLINE
    void Wfi()
    {
        asm("hlt");
    }

    SL_ALWAYS_INLINE
    size_t TrapFrameArgCount()
    {
        return 6;
    }

    SL_ALWAYS_INLINE
    size_t SyscallFrameArgCount()
    {
        return 6;
    }

    SL_ALWAYS_INLINE
    bool InterruptsEnabled()
    {
        uint64_t flags;
        asm volatile("pushf; pop %0" : "=rm"(flags));
        return flags & (1 << 9);
    }

    SL_ALWAYS_INLINE
    void EnableInterrupts()
    {
        asm("sti" ::: "cc");
    }

    SL_ALWAYS_INLINE
    void DisableInterrupts()
    {
        asm("cli" ::: "cc");
    }
}
