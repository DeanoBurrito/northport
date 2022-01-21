#pragma once

namespace sl
{
    template<typename T>
    struct Vector2
    {
        T x;
        T y;

        Vector2() : x(0), y(0)
        {}

        Vector2(T a) : x(a), y(a)
        {}

        Vector2(T x, T y) : x(x), y(y)
        {}

        bool operator==(const Vector2& other)
        { return x == other.x && y == other.y; }
        
        bool operator!=(const Vector2& other)
        { return x != other.x || y != other.y; }
    };

    using Vector2i = Vector2<long long>;
    using Vector2u = Vector2<unsigned long long>;
    using Vector2f = Vector2<float>;
    using Vector2d = Vector2<double>;

    template<typename T>
    struct Vector3
    {
        T x;
        T y;
        T z;

        Vector3() : x(0), y(0), z(0)
        {}

        Vector3(T a) : x(a), y(a), z(a)
        {}

        Vector3(T x, T y, T z) : x(x), y(y), z(z)
        {}

        bool operator==(const Vector3& other)
        { return x == other.x && y == other.y && z == other.z; }

        bool operator!=(const Vector3& other)
        { return x != other.x || y != other.y || z != other.z; }
    };

    using Vector3i = Vector3<long long>;
    using Vector3u = Vector3<unsigned long long>;
    using Vector3f = Vector3<float>;
    using Vector3d = Vector3<double>;

    template<typename T>
    struct Vector4
    {
        T x;
        T y;
        T z;
        T w;

        Vector4() : x(0), y(0), z(0), w(0)
        {}

        Vector4(T a) : x(a), y(a), z(a), w(a)
        {}

        Vector4(T x, T y, T z, T w) : x(x), y(y), z(z), w(w)
        {}

        bool operator==(const Vector4& other)
        { return x == other.x && y == other.y && z == other.z && w == other.w; }

        bool operator!=(const Vector4& other)
        { return x != other.x || y != other.y || z != other.z || w != other.w; }
    };

    using Vector4i = Vector4<long long>;
    using Vector4u = Vector4<unsigned long long>;
    using Vector4f = Vector4<float>;
    using Vector4d = Vector4<double>;
}
