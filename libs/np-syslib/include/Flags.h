#pragma once

namespace sl
{
    template<typename T, typename BackingInt = unsigned long>
    struct Flags
    {
    private:
        BackingInt value;
    
    public:
        constexpr Flags() : value{}
        {}

        constexpr Flags(BackingInt rawInit) : value(rawInit)
        {}

        constexpr Flags(T initial) : value((BackingInt)1 << (BackingInt)initial)
        {}

        constexpr bool Has(T index) const
        {
            return (BackingInt)index & value;
        }

        constexpr bool Set(T index)
        {
            const BackingInt mask = (BackingInt)1 << (BackingInt)index;
            const bool prev = value & mask;
            value |= mask;
            return !prev;
        }

        constexpr bool Clear(T index)
        {
            const BackingInt mask = (BackingInt)1 << (BackingInt)index;
            const bool prev = value & mask;
            value &= ~mask;
            return prev;
        }

        constexpr void Reset()
        { value = 0; }

        constexpr bool Any()
        { return value != 0; }

        constexpr Flags operator!()
        { return !value; }

        constexpr Flags operator|(const Flags other)
        { return value | other.value; }

        constexpr Flags operator&(const Flags other)
        { return value & other.value; }

        constexpr Flags& operator|=(const Flags other) 
        {
            value |= other.value;
            return *this;
        }

        constexpr Flags& operator&=(const Flags other)
        {
            value &= other.value;
            return *this;
        }

        //NOTE: using this defeats the whole purpose of this class, this function
        //is for printing debug output.
        constexpr BackingInt Raw()
        { return value; }
    };
}

