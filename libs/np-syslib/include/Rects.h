#pragma once

#include <Vectors.h>

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

        constexpr Rect(Vector4<T> details) : left(details.x), top(details.y), width(details.z), height(details.w)
        {}

        constexpr Rect(Vector2<T> topLeft, Vector2<T> size) 
        : left(topLeft.x), top(topLeft.y), width(size.x), height(size.y)
        {}

        bool Intersects(Rect<T> other) const
        {
            if (left > other.left + other.width)
                return false;
            if (left + width < other.left)
                return false;
            if (top > other.top + other.height)
                return false;
            if (top + height < other.top)
                return false;
            return true;
        }

        Vector2<T> TopLeft() const
        { return { left, top }; }

        Vector2<T> Size() const
        { return { width, height }; }

        Vector2<T> BotRight() const
        { return { left + width, top + height}; }

        Vector4<T> ToVector4() const
        { return { left, top, width, height }; }
    };

    using IntRect = Rect<long>;
    using UIntRect = Rect<unsigned long>;
    using FloatRect = Rect<float>;
}
