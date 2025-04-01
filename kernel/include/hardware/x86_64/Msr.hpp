#pragma once

#include <Types.h>
#include <Compiler.h>

namespace Npk
{
    enum class Msr : uint32_t
    {
        ApicBase = 0x1B,
        Tsc = 0x10,
        TscDeadline = 0x6E0,
        Efer = 0xC0000080,
        GsBase = 0xC0000101,
        KernelGsBase = 0xC0000102,
        PvSystemTime = 0x4B564D01,
        PvWallClock = 0x4B564D00,
    };

    SL_ALWAYS_INLINE
    uint64_t ReadMsr(Msr which)
    {
        uint32_t high, low;
        asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(static_cast<uint32_t>(which)) : "memory");
        return ((uint64_t)high << 32) | low;
    }

    SL_ALWAYS_INLINE
    void WriteMsr(Msr which, uint64_t data)
    {
        asm volatile("wrmsr" :: "a"(data & 0xFFFF'FFFF), "d"(data >> 32), "c"(static_cast<uint32_t>(which)));
    }
}
