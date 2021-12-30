#include <gfx/GraphicsPrimitives.h>
#include <Memory.h>

namespace Kernel::Gfx
{
    PackedColourFormat Colour::packedFormatDefault;

    void Colour::Translate(uint32_t* source, PackedColourFormat srcFormat, uint32_t* destination, PackedColourFormat destFormat, size_t pixelCount)
    {
        if (source == nullptr || destination == nullptr || pixelCount == 0)
            return;

        if (srcFormat == destFormat)
        {
            sl::memcopy(source, destination, pixelCount * sizeof(uint32_t));
            return;
        }

        //try and collapse the reserved/non-reserved versions - reduce the complexity of whats to come
        bool ignoreLsb = false;
        bool ignoreMsb = false;
        switch (srcFormat)
        {
        case PackedColourFormat::RGBr_8bpp:
            ignoreLsb = true;
            srcFormat = PackedColourFormat::RGBA_8bpp;
            break;
        case PackedColourFormat::BGRr_8bpp:
            ignoreLsb = true;
            srcFormat = PackedColourFormat::BGRA_8bpp;
            break;
        case PackedColourFormat::rRGB_8bpp:
            ignoreMsb = true;
            srcFormat = PackedColourFormat::ARGB_8bpp;
            break;
        case PackedColourFormat::rBGR_8bpp:
            ignoreMsb = true;
            srcFormat = PackedColourFormat::ABGR_8bpp;
            break;
        default:
            break;
        }

        bool zeroLsb = false;
        bool zeroMsb = false;
        switch (destFormat)
        {
        case PackedColourFormat::RGBr_8bpp:
            zeroLsb = true;
            destFormat = PackedColourFormat::RGBA_8bpp;
            break;
        case PackedColourFormat::BGRr_8bpp:
            zeroLsb = true;
            destFormat = PackedColourFormat::BGRA_8bpp;
            break;
        case PackedColourFormat::rRGB_8bpp:
            zeroMsb = true;
            destFormat = PackedColourFormat::ARGB_8bpp;
            break;
        case PackedColourFormat::rBGR_8bpp:
            zeroMsb = true;
            destFormat = PackedColourFormat::ABGR_8bpp;
            break;
        default:
            break;
        }

        uint32_t readMask = 0x00FF'FF00  | (ignoreMsb ? 0 : 0xFF00'0000) | (ignoreLsb ? 0 : 0xFF);
        uint32_t writeMask = 0x00FF'FF00  | (zeroMsb ? 0 : 0xFF00'0000) | (zeroLsb ? 0 : 0xFF);

        uint8_t r, g, b, a;
        uint32_t scratch;
        for (size_t i = 0; i < pixelCount; i++)
        {
            //there's likely a way to avoid this copy - would be to check compiler explorer and see whats being generated here
            scratch = source[i] & readMask;
            
            switch (srcFormat)
            {
            case PackedColourFormat::RGBA_8bpp:
                r = scratch >> 24;
                g = (scratch >> 16) & 0xFF;
                b = (scratch >> 8) & 0xFF;
                a = (scratch >> 0) & 0xFF;
                break;
            case PackedColourFormat::BGRA_8bpp:
                b = scratch >> 24;
                g = (scratch >> 16) & 0xFF;
                r = (scratch >> 8) & 0xFF;
                a = (scratch >> 0) & 0xFF;
                break;
            case PackedColourFormat::ARGB_8bpp:
                a = scratch >> 24;
                r = (scratch >> 16) & 0xFF;
                g = (scratch >> 8) & 0xFF;
                b = (scratch >> 0) & 0xFF;
                break;
            case PackedColourFormat::ABGR_8bpp:
                a = scratch >> 24;
                b = (scratch >> 16) & 0xFF;
                g = (scratch >> 8) & 0xFF;
                r = (scratch >> 0) & 0xFF;
                break;
            default: //shouldnt happen, unless we add more formats, but we'll know as all colour format translation will just break here.
                r = g = b = a = 0;
                continue;
            }

            destination[i] = Colour(r, g, b, a).GetPacked(destFormat) & writeMask;
        }
    }
    
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

        default:
            return 0;
        }
    }
}