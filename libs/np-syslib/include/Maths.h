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
        return (addr / alignment) * alignment;
    }

    template<typename T>
    constexpr inline T AlignUp(T addr, size_t alignment)
    {
        return ((addr + alignment - 1) / alignment) * alignment;
    }

    template<typename T>
    constexpr inline T* AlignDown(T* addr, size_t alignment)
    {
        return reinterpret_cast<T*>(((uintptr_t)addr / alignment) * alignment);
    }

    template<typename T>
    constexpr inline T* AlignUp(T* addr, size_t alignment)
    {
        return reinterpret_cast<T*>(((uintptr_t)(addr + alignment - 1) / alignment) * alignment);
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

    template<typename T>
    constexpr inline T Clamp(T value, T low, T high)
    {
        if (value < low)
            return low;
        if (value > high)
            return high;
        return value;
    }

    constexpr inline uint8_t ByteSwap(uint8_t value)
    { return value; }

    constexpr inline uint16_t ByteSwap(uint16_t value)
    { 
        return (value << 8) | (value >> 8);
    }

    constexpr inline uint32_t ByteSwap(uint32_t value)
    { 
        uint32_t result = 0;
        result |= (value & 0x0000'00FF) << 24;
        result |= (value & 0x0000'FF00) << 8;
        result |= (value & 0x00FF'0000) >> 8;
        result |= (value & 0xFF00'0000) >> 24;
        return result;
    }

    constexpr inline uint64_t ByteSwap(uint64_t value)
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

    template<typename T>
    constexpr inline T SquareRoot(T value)
    {
        if (value == 0)
            return 0;
        T left = 1;
        T right = value / 2 + 1;
        T res = 0;

        while (left <= right)
        {
            T mid = left + ((right - left) / 2);
            if (mid <= value / mid)
            {
                left = mid + 1;
                res = mid;
            }
            else
                right = mid - 1;
        }

        return res;
    }
    
    template<typename T> //mm yes, very safe.
    constexpr inline T StandardDeviation(T* data, size_t dataCount)
    {
        T mean = 0;
        for (size_t i = 0; i < dataCount; i++)
            mean += data[i];
        mean = mean / dataCount;
        
        T variance = 0;
        for (size_t i = 0; i < dataCount; i++)
        {
            variance += ((data[i] - mean) * (data[i] - mean) / (dataCount - 1));
        }

        return SquareRoot(variance);
    }
}
