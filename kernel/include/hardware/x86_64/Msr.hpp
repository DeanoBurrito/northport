#pragma once

#include <Types.hpp>
#include <Compiler.hpp>
#include <Span.hpp>

namespace Npk
{
    enum class Msr : uint32_t
    {
        ApicBase = 0x1B,
        Tsc = 0x10,
        MtrrCap = 0xFE,
        SysenterCs = 0x174,
        SysenterRsp = 0x175,
        SysenterRip = 0x176,
        MtrrPhysBase = 0x200,
        MtrrPhysMask = 0x201,
        MtrrDefType = 0x2FF,
        TscDeadline = 0x6E0,
        X2ApicBase = 0x800,
        Efer = 0xC0000080,
        Star = 0xC0000081,
        LStar = 0xC0000082,
        CStar = 0xC0000083,
        SFMask = 0xC0000084,
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

    void SaveMtrrs(sl::Span<uint64_t> regs);
    void RestoreMtrrs(sl::Span<uint64_t> regs);
}
