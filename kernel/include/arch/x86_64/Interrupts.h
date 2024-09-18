#pragma once

#include <stdint.h>
#include <stddef.h>
#include <interfaces/intra/Compiler.h>

namespace Npk
{
    ALWAYS_INLINE
    void Wfi()
    {
        asm("hlt");
    }

    ALWAYS_INLINE
    size_t TrapFrameArgCount()
    {
        return 6;
    }

    ALWAYS_INLINE
    bool InterruptsEnabled()
    {
        uint64_t flags;
        asm volatile("pushf; pop %0" : "=rm"(flags));
        return flags & (1 << 9);
    }

    ALWAYS_INLINE
    void EnableInterrupts()
    {
        asm("sti" ::: "cc");
    }

    ALWAYS_INLINE
    void DisableInterrupts()
    {
        asm("cli" ::: "cc");
    }
}
