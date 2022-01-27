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
}