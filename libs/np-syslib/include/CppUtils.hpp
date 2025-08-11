#pragma once

namespace sl
{
    template<typename T>
    struct _RemoveReference
    { using Type = T; };

    template<typename T>
    struct _RemoveReference<T&>
    { using Type = T; };

    template<typename T>
    struct _RemoveReference<T&&>
    { using Type = T; };

    template<typename T>
    using RemoveReferenceType = typename _RemoveReference<T>::Type;

    template<typename T>
    constexpr inline bool IsLValueReference = false;
    
    template<typename T>
    constexpr inline bool IsLValueReference<T&> = true;

    template<bool B, typename T = void>
    struct EnableIf
    {};

    template<typename T>
    struct EnableIf<true, T>
    { using Type = T; };

    template<typename T, typename U>
    constexpr inline bool IsSame = false;

    template<typename T>
    constexpr inline bool IsSame<T, T> = true;

    template<typename T>
    struct _MakeUnsigned
    { using Type = void; };

    template<>
    struct _MakeUnsigned<signed char>
    { using Type = unsigned char; };

    template<>
    struct _MakeUnsigned<short>
    { using Type = unsigned short; };

    template<>
    struct _MakeUnsigned<int>
    { using Type = unsigned int; };

    template<>
    struct _MakeUnsigned<long>
    { using Type = unsigned long; };

    template<>
    struct _MakeUnsigned<long long>
    { using Type = unsigned long long; };

    template<>
    struct _MakeUnsigned<unsigned char>
    { using Type = unsigned char; };

    template<>
    struct _MakeUnsigned<unsigned short>
    { using Type = unsigned short; };

    template<>
    struct _MakeUnsigned<unsigned int>
    { using Type = unsigned int; };

    template<>
    struct _MakeUnsigned<unsigned long>
    { using Type = unsigned long; };

    template<>
    struct _MakeUnsigned<unsigned long long>
    { using Type = unsigned long long; };

    template<>
    struct _MakeUnsigned<char>
    { using Type = unsigned char; };

    template<>
    struct _MakeUnsigned<bool>
    { using Type = bool; };

    template<typename T>
    using MakeUnsigned = typename _MakeUnsigned<T>::Type;

    template<typename T>
    constexpr inline bool IsUnsigned = IsSame<T, MakeUnsigned<T>>;

    template<typename T>
    constexpr RemoveReferenceType<T>&& Move(T&& t) noexcept
    { return static_cast<RemoveReferenceType<T>&&>(t); }

    template<typename T>
    void Swap(T& a, T& b)
    {
        T temp = Move(a);
        a = Move(b);
        b = Move(temp);
    }

    template<typename T>
    constexpr T&& Forward(RemoveReferenceType<T>& param)
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
