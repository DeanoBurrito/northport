#pragma once

namespace sl
{
    template<typename T>
    struct Vector2
    {
        T x;
        T y;

        constexpr Vector2() : x(0), y(0)
        {}

        constexpr Vector2(T a) : x(a), y(a)
        {}

        constexpr Vector2(T x, T y) : x(x), y(y)
        {}
        
        constexpr Vector2& operator+=(const Vector2& b)
        {
            x += b.x;
            y += b.y;
            return *this;
        }

        constexpr Vector2& operator-=(const Vector2& b)
        {
            x -= b.x;
            y -= b.y;
            return *this;
        }

        constexpr Vector2& operator*=(const Vector2& b)
        {
            x *= b.x;
            y *= b.y;
            return *this;
        }

        constexpr Vector2& operator/=(const Vector2& b)
        {
            x /= b.x;
            y /= b.y;
            return *this;
        }

        constexpr T operator[](unsigned index)
        {
            switch (index)
            {
                case 0: return x;
                case 1: return y;
                case 'x': return x; //very cursed, but I really like it.
                case 'y': return y; //I impress myself sometimes hahaha.
                default: return 0;
            }
        }

        template<typename U>
        constexpr explicit operator Vector2<U>()
        { return { (U)x, (U)y }; }
    };

    template<typename T>
    constexpr bool operator==(const Vector2<T> a, const Vector2<T>& b)
    { return a.x == b.x && a.y == b.y; }
    
    template<typename T>
    constexpr bool operator!=(const Vector2<T> a, const Vector2<T>& b)
    { return a.x != b.x || a.y != b.y; }

    template<typename T>
    constexpr Vector2<T> operator+(const Vector2<T> a, const Vector2<T>& b)
    { return { a.x + b.x , a.y + b.y }; }

    template<typename T>
    constexpr Vector2<T> operator-(const Vector2<T> a, const Vector2<T>& b)
    { return { a.x - b.x , a.y - b.y }; }

    template<typename T>
    constexpr Vector2<T> operator*(const Vector2<T> a, const Vector2<T>& b)
    { return { a.x * b.x , a.y * b.y }; }

    template<typename T>
    constexpr Vector2<T> operator/(const Vector2<T> a, const Vector2<T>& b)
    { return { a.x / b.x , a.y / b.y }; }

    using Vector2i = Vector2<long>;
    using Vector2u = Vector2<unsigned long>;
    using Vector2f = Vector2<float>;
    using Vector2d = Vector2<double>;

    template<typename T>
    struct Vector3
    {
        T x;
        T y;
        T z;

        constexpr Vector3() : x(0), y(0), z(0)
        {}

        constexpr Vector3(T a) : x(a), y(a), z(a)
        {}

        constexpr Vector3(T x, T y, T z) : x(x), y(y), z(z)
        {}

        constexpr bool operator==(const Vector3& other)
        { return x == other.x && y == other.y && z == other.z; }

        constexpr bool operator!=(const Vector3& other)
        { return x != other.x || y != other.y || z != other.z; }

        constexpr Vector3& operator+=(const Vector3& b)
        {
            x += b.x;
            y += b.y;
            z += b.z;
            return *this;
        }

        constexpr Vector3& operator-=(const Vector3& b)
        {
            x -= b.x;
            y -= b.y;
            z -= b.z;
            return *this;
        }

        constexpr Vector3& operator*=(const Vector3& b)
        {
            x *= b.x;
            y *= b.y;
            z *= b.z;
            return *this;
        }

        constexpr Vector3& operator/=(const Vector3& b)
        {
            x /= b.x;
            y /= b.y;
            z /= b.z;
            return *this;
        }

        constexpr T operator[](unsigned index)
        {
            switch (index)
            {
                case 0: return x;
                case 1: return y;
                case 2: return z;
                case 'x': return x;
                case 'y': return y;
                case 'z': return z;
                default: return 0;
            }
        }

        template<typename U>
        constexpr explicit operator Vector3<U>()
        { return { (U)x, (U)y, (U)z }; }
    };

    template<typename T>
    constexpr bool operator==(const Vector3<T> a, const Vector3<T>& b)
    { return a.x == b.x && a.y == b.y && a.z == b.z; }
    
    template<typename T>
    constexpr bool operator!=(const Vector3<T> a, const Vector3<T>& b)
    { return a.x != b.x || a.y != b.y || a.z != b.z; }

    template<typename T>
    constexpr Vector3<T> operator+(const Vector3<T> a, const Vector3<T>& b)
    { return { a.x + b.x , a.y + b.y, a.z + b.z }; }

    template<typename T>
    constexpr Vector3<T> operator-(const Vector3<T> a, const Vector3<T>& b)
    { return { a.x - b.x , a.y - b.y, a.z - b.z }; }

    template<typename T>
    constexpr Vector3<T> operator*(const Vector3<T> a, const Vector3<T>& b)
    { return { a.x * b.x , a.y * b.y, a.z * b.z }; }

    template<typename T>
    constexpr Vector3<T> operator/(const Vector3<T> a, const Vector3<T>& b)
    { return { a.x / b.x , a.y / b.y, a.z / b.z }; }

    using Vector3i = Vector3<long>;
    using Vector3u = Vector3<unsigned long>;
    using Vector3f = Vector3<float>;
    using Vector3d = Vector3<double>;

    template<typename T>
    struct Vector4
    {
        T x;
        T y;
        T z;
        T w;

        constexpr Vector4() : x(0), y(0), z(0), w(0)
        {}

        constexpr Vector4(T a) : x(a), y(a), z(a), w(a)
        {}

        constexpr Vector4(T x, T y, T z, T w) : x(x), y(y), z(z), w(w)
        {}

        constexpr bool operator==(const Vector4& other)
        { return x == other.x && y == other.y && z == other.z && w == other.w; }

        constexpr bool operator!=(const Vector4& other)
        { return x != other.x || y != other.y || z != other.z || w != other.w; }

        constexpr Vector4& operator+=(const Vector4& b)
        {
            x += b.x;
            y += b.y;
            z += b.z;
            w += b.w;
            return *this;
        }

        constexpr Vector4& operator-=(const Vector4& b)
        {
            x -= b.x;
            y -= b.y;
            z -= b.z;
            w -= b.w;
            return *this;
        }

        constexpr Vector4& operator*=(const Vector4& b)
        {
            x *= b.x;
            y *= b.y;
            z *= b.z;
            w *= b.w;
            return *this;
        }

        constexpr Vector4& operator/=(const Vector4& b)
        {
            x /= b.x;
            y /= b.y;
            z /= b.z;
            w /= b.w;
            return *this;
        }

        constexpr T operator[](unsigned index)
        {
            switch (index)
            {
                case 0: return x;
                case 1: return y;
                case 2: return z;
                case 3: return w;
                case 'x': return x;
                case 'y': return y;
                case 'z': return z;
                case 'w': return w;
                default: return 0;
            }
        }

        template<typename U>
        constexpr explicit operator Vector4<U>()
        { return { (U)x, (U)y, (U)z, (U)w }; }
    };

    using Vector4i = Vector4<long>;
    using Vector4u = Vector4<unsigned long>;
    using Vector4f = Vector4<float>;
    using Vector4d = Vector4<double>;
}
