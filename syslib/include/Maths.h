#pragma once

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
}