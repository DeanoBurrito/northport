#pragma once

namespace sl
{
    template<typename T>
    struct Flags
    {
    private:
        using BackingInt = unsigned long;
        BackingInt value;
    
    public:
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

        inline constexpr bool Clear(T index)
        {
            const BackingInt mask = (BackingInt)1 << (BackingInt)index;
            const bool prev = value & mask;
            value &= ~mask;
            return prev;
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

        //NOTE: using this defeats the whole purpose of this class, this function
        //is for printing debug output.
        inline constexpr BackingInt Raw() const
        { return value; }
    };
}

template<typename T>
inline constexpr sl::Flags<T> operator|(T a, T b)
{
    return sl::Flags<T>(a) | b;
}

