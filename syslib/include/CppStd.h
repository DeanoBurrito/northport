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
