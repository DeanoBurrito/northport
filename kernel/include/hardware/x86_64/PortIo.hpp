#pragma once

#include <Types.hpp>
#include <Compiler.hpp>

namespace Npk
{
    enum class Port : uint16_t
    {
        Pic0Command = 0x20,
        Pic0Data = 0x21,
        PitData0 = 0x40,
        PitCommand = 0x43,
        Pic1Command = 0xA0,
        Pic1Data = 0xA1,
        Debugcon = 0xE9,
        Com1 = 0x3F8,
    };

    SL_ALWAYS_INLINE
    uint8_t In8(Port port)
    { 
        uint8_t value;
        asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
        return value;
    }

    SL_ALWAYS_INLINE
    uint16_t In16(Port port)
    {
        uint16_t value;
        asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port) : "memory");
        return value;
    }

    SL_ALWAYS_INLINE
    uint32_t In32(Port port)
    {
        uint32_t value;
        asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port) : "memory");
        return value;
    }

    SL_ALWAYS_INLINE
    void Out8(Port port, uint8_t data)
    {
        asm volatile("outb %0, %1" :: "a"(data), "Nd"(port));
    }

    SL_ALWAYS_INLINE
    void Out16(Port port, uint16_t data)
    {
        asm volatile("outw %0, %1" :: "a"(data), "Nd"(port));
    }

    SL_ALWAYS_INLINE
    void Out32(Port port, uint32_t data)
    {
        asm volatile("outl %0, %1" :: "a"(data), "Nd"(port));
    }
}
