#pragma once

#include <Compiler.h>

namespace sl
{
    template<typename T, typename BackingInt = unsigned long, unsigned Alignment = alignof(BackingInt)>
    struct SL_PACKED(alignas(Alignment) Flags
    {
        alignas(alignof(BackingInt)) BackingInt value;

        constexpr Flags() : value{}
        {}

        constexpr Flags(BackingInt raw) : value(raw)
        {}

        constexpr Flags(T initial) : value((BackingInt)1 << (BackingInt)initial)
        {}

        inline constexpr bool Has(T index) const
        {
            const BackingInt mask = (BackingInt)1 << (BackingInt)index;
            return value & mask;
        }

        inline constexpr bool Set(T index)
        {
            const BackingInt mask = (BackingInt)1 << (BackingInt)index;
            const bool prev = value & mask;
            value |= mask;
            return !prev;
        }

        inline constexpr Flags& SetThen(T index)
        {
            Set(index);
            return *this;
        }

        inline constexpr bool Clear(T index)
        {
            const BackingInt mask = (BackingInt)1 << (BackingInt)index;
            const bool prev = value & mask;
            value &= ~mask;
            return prev;
        }

        inline constexpr Flags& ClearThen(T index)
        {
            Clear(index);
            return *this;
        }

        inline constexpr void Reset()
        { value = 0; }

        inline constexpr bool Any() const
        { return value != 0; }

        inline constexpr Flags operator!() const
        { return !value; }

        inline constexpr Flags operator|(const Flags other) const
        { return value | other.value; }

        inline constexpr Flags operator|(const T index) const
        {
            const BackingInt mask = (BackingInt)1 << (BackingInt)index;
            return value | mask;
        }

        inline constexpr Flags operator&(const Flags other) const
        { return value & other.value; }

        inline constexpr Flags operator&(const T index) const
        {
            const BackingInt mask = (BackingInt)1 << (BackingInt)index;
            return value & mask;
        }

        inline constexpr Flags& operator|=(const Flags other) 
        {
            value |= other.value;
            return *this;
        }

        inline constexpr Flags& operator&=(const Flags other)
        {
            value &= other.value;
            return *this;
        }

        inline constexpr bool operator==(const Flags other) const
        { return value == other.value; }

        inline constexpr bool operator!=(const Flags other) const
        { return value != other.value; }
    });
}

template<typename T>
inline constexpr sl::Flags<T> operator|(T a, T b)
{
    return sl::Flags<T>(a) | b;
}

