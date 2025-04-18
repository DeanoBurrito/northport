#pragma once

#include <Types.h>
#include <Compiler.h>

namespace Npk
{
    enum class Port : uint16_t
    {
        PitData = 0x40,
        PitCommand = 0x43,
        Debugcon = 0xE9,
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
