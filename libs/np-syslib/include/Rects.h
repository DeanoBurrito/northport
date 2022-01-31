#pragma once

namespace sl
{
    template<typename T>
    struct Rect
    {
        T left;
        T top;
        T width;
        T height;

        constexpr Rect() : left(0), top(0), width(0), height(0)
        {}

        constexpr Rect(T w, T h) : left(0), top(0), width(w), height(h)
        {}

        constexpr Rect(T left, T top, T width, T height) : left(left), top(top), width(width), height(height)
        {}
    };

    using IntRect = Rect<long>;
    using UIntRect = Rect<unsigned long>;
    using FloatRect = Rect<float>;
}
