#pragma once

#include <stdint.h>

namespace Kernel::Gfx
{
    template<typename T>
    struct Vector2
    {
        T x;
        T y;

        bool operator==(const Vector2& other)
        { return x == other.x && y == other.y; }

        bool operator!=(const Vector2& other)
        { return x != other.x || y != other.y; }
    };

    using Vector2i = Vector2<long>;
    using Vector2u = Vector2<unsigned long>;

    template<typename T>
    struct Vector3
    {
        T x;
        T y;
        T z;

        bool operator==(const Vector3& other)
        { return x == other.x && y = other.y && z == other.z; }

        bool operator!=(const Vector3& other)
        { return x != other.x || y != other.y || z != other.z; }
    };

    using Vector3i = Vector3<long>;
    using Vector3u = Vector3<unsigned long>;

    template<typename T>
    struct Rect
    {
        T left;
        T top;
        T width;
        T height;
    };

    using IntRect = Rect<long>;

    enum class PackedColourFormat
    {
        RGBA_8bpp,
        RGBr_8bpp,
        BGRA_8bpp,
        BGRr_8bpp,
        ARGB_8bpp,
        rRGB_8bpp,
        ABGR_8bpp,
        rBGR_8bpp,
    };

    //yes, colour. I speak the real english.
    struct Colour
    {   
        static PackedColourFormat packedFormatDefault;
        
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;

        Colour() = default;

        constexpr Colour(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b), a(0xFF)
        {}

        constexpr Colour(uint8_t r, uint8_t g, uint8_t b, uint8_t a) : r(r), g(g), b(b), a(a)
        {}

        constexpr Colour(uint32_t rgba) : r(rgba >> 24), g((rgba >> 16) & 0xFF), b((rgba >> 8) & 0xFF), a(rgba & 0xFF)
        {}

        uint32_t GetPacked(PackedColourFormat format = packedFormatDefault);
    };
    //but I'll forgive you if you dont.
    //using Color = Colour;

    namespace Colours
    {
        constexpr static inline Colour Black        = Colour(0x000000FF);
        constexpr static inline Colour White        = Colour(0xFFFFFFFF);
        constexpr static inline Colour Red          = Colour(0xFF0000FF);
        constexpr static inline Colour Green        = Colour(0x00FF00FF);
        constexpr static inline Colour Blue         = Colour(0x0000FFFF);
        constexpr static inline Colour Yellow       = Colour(0xFFFF00FF);
        constexpr static inline Colour Cyan         = Colour(0x00FFFFFF);
        constexpr static inline Colour Magenta      = Colour(0xFF00FFFF);
        constexpr static inline Colour Grey         = Colour(0x888888FF);
        constexpr static inline Colour DarkRed      = Colour(0x880000FF);
        constexpr static inline Colour DarkGreen    = Colour(0x008800FF);
        constexpr static inline Colour DarkBlue     = Colour(0x000088FF);
        constexpr static inline Colour DarkYellow   = Colour(0x888800FF);
        constexpr static inline Colour DarkCyan     = Colour(0x008888FF);
        constexpr static inline Colour DarkMagenta  = Colour(0x880088FF);
        constexpr static inline Colour DarkGrey     = Colour(0x444444FF);
        constexpr static inline Colour Transparent  = Colour(0xFFFFFF00);
    }
}
