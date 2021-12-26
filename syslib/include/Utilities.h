#pragma once

#include <stddef.h>
#include <stdint.h>
#include <NativePtr.h>
#include <CppStd.h>

namespace sl
{
    [[gnu::always_inline]]
    inline bool BitmapGet(uint8_t* base, size_t index)
    {
        const size_t byteIndex = index / 8;
        const size_t bitIndex = index % 8;
        return (*sl::NativePtr(base).As<uint8_t>(byteIndex) & (1 << bitIndex)) != 0;
    }

    [[gnu::always_inline]]
    inline void BitmapSet(uint8_t* base, size_t index)
    {
        const size_t byteIndex = index / 8;
        const size_t bitIndex = index % 8;
        *sl::NativePtr(base).As<uint8_t>(byteIndex) |= (1 << bitIndex);
    }

    [[gnu::always_inline]]
    inline void BitmapClear(uint8_t* base, size_t index)
    {
        const size_t byteIndex = index / 8;
        const size_t bitIndex = index % 8;
        *sl::NativePtr(base).As<uint8_t>(byteIndex) &= ~(1 << bitIndex);
    }
    
    template<typename F, typename E>
    [[gnu::always_inline]]
    inline bool EnumHasFlag(F enumeration, E entry)
    {
        return ((size_t)enumeration & (size_t)entry) != 0;
    }

    template<typename F, typename E>
    [[gnu::always_inline]]
    inline F EnumSetFlag(F enumeration, E entry)
    {
        return (F)((size_t)enumeration | (size_t)entry);
    }

    template<typename F, typename E>
    [[gnu::always_inline]]
    inline F EnumClearFlag(F enumeration, E entry)
    {
        return (F)((size_t)enumeration & ~(size_t)entry);
    }

    template<typename F, typename E>
    [[gnu::always_inline]]
    inline F EnumSetFlagState(F enumeration, E entry, bool set)
    {
        if (set)
            return EnumSetFlag(enumeration, entry);
        else
            return EnumClearFlag(enumeration, entry);
    }

    template<typename T>
    T&& Move(T&& t)
    { return static_cast<T&&>(t); }

    template<typename T>
    void Swap(T& a, T& b)
    {
        T temp = Move(a);
        a = Move(b);
        b = Move(temp);
    }

    template<typename T>
    constexpr T&& Foward(RemoveReferenceType<T>& param)
    { return static_cast<T&&>(param); }

    template<typename T>
    constexpr T&& Forward(RemoveReferenceType<T>&& param)
    {
        static_assert(!IsLValueReference<T>, "Cant forward rvalue as lvalue.");;
        return static_cast<T&&>(param);
    }

    template<typename T>
    constexpr T* Launder(T* p)
    {
        return __builtin_launder(p);
    }
}
