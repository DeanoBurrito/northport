#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Npk
{
    struct TrapFrame
    {
    };

    static_assert(sizeof(TrapFrame) == 1, "m68k TrapFrame size changed, update assembly sources.");

    constexpr inline size_t PageSize = 0x1000;
    constexpr inline size_t TrapFrameArgCount = 6;
    constexpr inline size_t IntVectorAllocBase = 0x40;
    constexpr inline size_t IntVectorAllocLimit = 0xFF;

    [[gnu::always_inline]]
    inline void Wfi()
    { asm("stop #0x2000"); }

    [[gnu::always_inline]]
    inline bool InterruptsEnabled()
    { return true; }

    [[gnu::always_inline]]
    inline void EnableInterrupts()
    {}

    [[gnu::always_inline]]
    inline void DisableInterrupts()
    {}

    [[gnu::always_inline]]
    inline void AllowSumac()
    {}

    [[gnu::always_inline]]
    inline void BlockSumac()
    {}

    [[gnu::always_inline]]
    inline bool CoreLocalAvailable()
    { return true; }
}
