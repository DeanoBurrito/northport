#pragma once

#include <Compiler.h>
#include <hardware/x86_64/PortIo.hpp>

namespace Npk
{
    constexpr size_t PitFrequency = 1193182;

    SL_ALWAYS_INLINE
    uint16_t ReadPit()
    {
        Out8(Port::PitCommand, 0);
        uint16_t data = 0;
        data = In8(Port::PitData0);
        data |= static_cast<uint16_t>(In8(Port::PitData0)) << 8;

        return 0xFFFF - data;
    }

    SL_ALWAYS_INLINE
    void StartPit()
    {
        Out8(Port::PitCommand, 0x34);
        Out8(Port::PitData0, 0xFF);
        Out8(Port::PitData0, 0xFF);
    }
}
