#pragma once

#include <stdint.h>
#include <stddef.h>

namespace np::Graphics
{
    struct Colour
    {
    public:
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

        uint32_t Pack(size_t redOffset, size_t greenOffset, size_t blueOffset, size_t alphaOffset, uint8_t redMask, uint8_t greenMask, uint8_t blueMask, uint8_t alphaMask);
    };

    using Color = Colour;

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
