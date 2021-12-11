#include <gfx/GraphicsPrimitives.h>

namespace Kernel::Gfx
{
    PackedColourFormat Colour::packedFormatDefault;
    
    uint32_t Colour::GetPacked(PackedColourFormat format)
    {
        switch (format)
        {
        case PackedColourFormat::RGBA_8bpp:
            return (uint32_t)r << 24 | (uint32_t)g << 16 | (uint32_t)b << 8 | a;
        case PackedColourFormat::RGBr_8bpp:
            return (uint32_t)r << 24 | (uint32_t)g << 16 | (uint32_t)b << 8;

        case PackedColourFormat::BGRA_8bpp:
            return (uint32_t)b << 24 | (uint32_t)g << 16 | (uint32_t)r << 8 | a;
        case PackedColourFormat::BGRr_8bpp:
            return (uint32_t)r << 24 | (uint32_t)g << 16 | (uint32_t)b << 8;
        
        case PackedColourFormat::ARGB_8bpp:
            return (uint32_t)a << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
        case PackedColourFormat::rRGB_8bpp:
            return (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;

        case PackedColourFormat::ABGR_8bpp:
            return (uint32_t)a << 24 | (uint32_t)b << 16 | (uint32_t)g << 8 | (uint32_t)r;
        case PackedColourFormat::rBGR_8bpp:
            return (uint32_t)b << 16 | (uint32_t)g << 8 | (uint32_t)r;
        }
    }
}