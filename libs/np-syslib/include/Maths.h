#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr inline size_t KiB = 1024;
constexpr inline size_t MiB = KiB * KiB;
constexpr inline size_t GiB = MiB * KiB;
constexpr inline size_t TiB = GiB * KiB;
constexpr inline size_t PiB = TiB * KiB;

namespace sl
{
    template<typename T>
    constexpr inline T AlignDown(T addr, size_t alignment)
    {
        return addr / alignment * alignment;
    }

    template<typename T>
    constexpr inline T AlignUp(T addr, size_t alignment)
    {
        return (addr + alignment - 1) / alignment * alignment;
    }

    template<typename T>
    constexpr inline T* AlignDown(T* addr, size_t alignment)
    {
        return reinterpret_cast<T*>((uintptr_t)addr / alignment * alignment);
    }

    template<typename T>
    constexpr inline T* AlignUp(T* addr, size_t alignment)
    {
        return reinterpret_cast<T*>(((uintptr_t)addr + alignment - 1) / alignment * alignment);
    }

    template<typename T>
    constexpr inline T Min(T a, T b)
    {
        return ((a < b) ? a : b);
    }

    template<typename T>
    constexpr inline T Max(T a, T b)
    {
        return ((a > b) ? a : b);
    }

    constexpr uint8_t ByteSwap(uint8_t value)
    { return value; }

    constexpr uint16_t ByteSwap(uint16_t value)
    { 
        return (value << 8) | (value >> 8);
    }

    constexpr uint32_t ByteSwap(uint32_t value)
    { 
        uint32_t result = 0;
        result |= (value & 0x0000'00FF) << 24;
        result |= (value & 0x0000'FF00) << 8;
        result |= (value & 0x00FF'0000) >> 8;
        result |= (value & 0xFF00'0000) >> 24;
        return result;
    }

    constexpr uint64_t ByteSwap(uint64_t value)
    { 
        uint64_t result = 0;
        result |= (value & 0x0000'0000'0000'00FF) << 56;
        result |= (value & 0x0000'0000'0000'FF00) << 40;
        result |= (value & 0x0000'0000'00FF'0000) << 24;
        result |= (value & 0x0000'0000'FF00'0000) << 8;
        result |= (value & 0x0000'00FF'0000'0000) >> 8;
        result |= (value & 0x0000'FF00'0000'0000) >> 24;
        result |= (value & 0x00FF'0000'0000'0000) >> 40;
        result |= (value & 0xFF00'0000'0000'0000) >> 56;
        return result;
    }
}
