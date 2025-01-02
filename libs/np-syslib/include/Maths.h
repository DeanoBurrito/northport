#pragma once

#include <Types.h>
#include <Span.h>

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

    constexpr inline uint8_t LeToHost(uint8_t value)
    {
        return value;
    }

    constexpr inline uint16_t LeToHost(uint16_t value)
    {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return value;
#else
        return ByteSwap(value);
#endif
    }

    constexpr inline uint32_t LeToHost(uint32_t value)
    {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return value;
#else
        return ByteSwap(value);
#endif
    }

    constexpr inline uint64_t LeToHost(uint64_t value)
    {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return value;
#else
        return ByteSwap(value);
#endif
    }

    constexpr inline uint8_t HostToLe(uint8_t value)
    { return LeToHost(value); }

    constexpr inline uint16_t HostToLe(uint16_t value)
    { return LeToHost(value); }

    constexpr inline uint32_t HostToLe(uint32_t value)
    { return LeToHost(value); }

    constexpr inline uint64_t HostToLe(uint64_t value)
    { return LeToHost(value); }

    constexpr inline uint8_t BeToHost(uint8_t value)
    {
        return value;
    }

    constexpr inline uint16_t BeToHost(uint16_t value)
    {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        return value;
#else
        return ByteSwap(value);
#endif
    }

    constexpr inline uint32_t BeToHost(uint32_t value)
    {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        return value;
#else
        return ByteSwap(value);
#endif
    }

    constexpr inline uint64_t BeToHost(uint64_t value)
    {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        return value;
#else
        return ByteSwap(value);
#endif
    }

    constexpr inline uint8_t HostToBe(uint8_t value)
    { return BeToHost(value); }

    constexpr inline uint16_t HostToBe(uint16_t value)
    { return BeToHost(value); }

    constexpr inline uint32_t HostToBe(uint32_t value)
    { return BeToHost(value); }

    constexpr inline uint64_t HostToBe(uint64_t value)
    { return BeToHost(value); }

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
    
    template<typename T>
    constexpr inline T StandardDeviation(sl::Span<T> data)
    {
        T mean = 0;
        for (size_t i = 0; i < data.Size(); i++)
            mean += data[i];
        mean = mean / data.Size();
        
        T variance = 0;
        for (size_t i = 0; i < data.Size(); i++)
            variance += ((data[i] - mean) * (data[i] - mean) / (data.Size() - 1));

        return SquareRoot(variance);
    }

    template<typename T>
    constexpr inline bool IsPowerOfTwo(T test)
    {
        return test && !(test & (test - 1));
    }

    template<typename T>
    constexpr inline T AlignUpBinary(T x)
    {
        x--;
        x |= x >> 1;
        x |= x >> 2;
        x |= x >> 4;
        x |= x >> 8;
        x |= x >> 16;
        if constexpr (sizeof(T) > 4)
            x |= x >> 32;
        if constexpr (sizeof(T) > 8)
            x |= x >> 64;
        return ++x;
    }

    template<typename T>
    constexpr inline T MaxOf(sl::Span<T> data)
    {
        T temp = data[0];
        for (size_t i = 1; i < data.Size(); i++)
        {
            if (data[i] > temp)
                temp = data[i];
        }

        return temp;
    }

    template<typename T>
    constexpr inline T MinOf(sl::Span<T> data)
    {
        T temp = data[0];
        for (size_t i = 1; i < data.Size(); i++)
        {
            if (data[i] < temp)
                temp = data[i];
        }

        return temp;
    }

    template<typename T>
    constexpr inline void MapRange(sl::Span<T> data, T oldMin, T oldMax, T newMin, T newMax)
    {
        if (oldMin == oldMax)
            return;
        if (oldMin == newMin && oldMax == newMax)
            return;

        T oldScale = oldMax - oldMin;
        T newScale = newMax - newMin;
        for (size_t i = 0; i < data.Size(); i++)
        {
            T temp = data[i] - oldMin;
            temp = (temp * newScale * 2 + oldScale) / oldScale / 2;
            data[i] = temp + newMin;
        }
    }

    template<typename T>
    constexpr inline void Normalize(sl::Span<T> data, T newMin, T newMax)
    {
        T oldMin = MinOf(data);
        T oldMax = MaxOf(data);
        MapRange(data, oldMin, oldMax, newMin, newMax);
    }

    constexpr inline size_t PopCount(size_t test)
    {
        /* This function was taken from https://github.com/mintsuki/cc-runtime, which is a rip
         * of the LLVM compiler runtime library (different flavour of libgcc).
         * See https://llvm.org/LICENSE.txt for the full license and more info.
         */
        uint64_t x2 = (uint64_t)test;
        x2 = x2 - ((x2 >> 1) & 0x5555555555555555uLL);
        x2 = ((x2 >> 2) & 0x3333333333333333uLL) + (x2 & 0x3333333333333333uLL);
        x2 = (x2 + (x2 >> 4)) & 0x0F0F0F0F0F0F0F0FuLL;
        uint32_t x = (uint32_t)(x2 + (x2 >> 32));
        x = x + (x >> 16);
        return (x + (x >> 8)) & 0x0000007F;
    }
}
