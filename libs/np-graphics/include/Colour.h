#pragma once

#include <stdint.h>
#include <stddef.h>

namespace np::Graphics
{
    struct ColourFormat
    {
        uint8_t redOffset;
        uint8_t greenOffset;
        uint8_t blueOffset;
        uint8_t alphaOffset;
        uint8_t redMask;
        uint8_t greenMask;
        uint8_t blueMask;
        uint8_t alphaMask;

        ColourFormat() = default;

        constexpr ColourFormat(uint8_t r_o, uint8_t g_o, uint8_t b_o, uint8_t a_o, uint8_t r_m, uint8_t g_m, uint8_t b_m, uint8_t a_m)
        : redOffset(r_o), greenOffset(g_o), blueOffset(b_o), alphaOffset(a_o), redMask(r_m), greenMask(g_m), blueMask(b_m), alphaMask(a_m)
        {}
    };

    constexpr static inline ColourFormat RGBA32(0, 8, 16, 24, 0xFF, 0xFF, 0xFF, 0xFF);
    
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

        uint32_t Pack(size_t redOffset, size_t greenOffset, size_t blueOffset, size_t alphaOffset, uint8_t redMask, uint8_t greenMask, uint8_t blueMask, uint8_t alphaMask) const;
        
        [[gnu::always_inline]] inline
        uint32_t Pack(ColourFormat f) const
        { return Pack(f.redOffset, f.greenOffset, f.blueOffset, f.alphaOffset, f.redMask, f.greenMask, f.blueMask, f.alphaMask); }
    };

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
