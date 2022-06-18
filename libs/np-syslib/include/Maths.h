#pragma once

#include <stdint.h>

constexpr uint64_t KB = 1024;
constexpr uint64_t MB = KB * KB;
constexpr uint64_t GB = MB * KB;
constexpr uint64_t TB = GB * KB;
constexpr uint64_t PB = TB * KB;

namespace sl
{
    template<typename T>
    [[gnu::always_inline]]
    constexpr inline T min(T a, T b)
    { return a < b ? a : b; }

    template<typename T>
    [[gnu::always_inline]]
    constexpr inline T max(T a, T b)
    { return a > b ? a : b; }

    template<typename T>
    [[gnu::always_inline]]
    constexpr inline T clamp(T v, T lower, T upper)
    {
        if (v < lower)
            return lower;
        if (upper < v)
            return upper;
        return v;
    }

    template<typename T>
    [[gnu::always_inline]]
    constexpr inline T _AbsInternal(T v, T zero)
    { return v < zero ? -v : v; }

    [[gnu::always_inline]]
    constexpr inline int Abs(int v)
    { return _AbsInternal(v, 0); }

    [[gnu::always_inline]]
    constexpr inline int Labs(long v)
    { return _AbsInternal(v, 0l); }

    [[gnu::always_inline]]
    constexpr inline int Llabs(long long v)
    { return _AbsInternal(v, 0ll); }

    constexpr bool IsBigEndian()
    {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        return true;
#else 
        return false;
#endif
    }

    constexpr bool IsLittleEndian()
    {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return true;
#else 
        return false;
#endif
    }

    constexpr uint8_t SwapEndianness(uint8_t value)
    { return value; }

    constexpr uint16_t SwapEndianness(uint16_t value)
    { return __builtin_bswap16(value); }

    constexpr uint32_t SwapEndianness(uint32_t value)
    { return __builtin_bswap32(value); }

    constexpr uint64_t SwapEndianness(uint64_t value)
    { return __builtin_bswap64(value); }
}