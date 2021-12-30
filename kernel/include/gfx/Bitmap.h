#pragma once

#include <gfx/GraphicsPrimitives.h>
#include <devices/SimpleFramebuffer.h>

namespace Kernel::Gfx
{
    /* 
        Does not correspond to any file format. This class is a resource container, of a 32bpp image. 
        Colour format varies, but will use the native framebuffer's default as it's own default.
    */
    class Bitmap
    {
    private:
        uint32_t* buffer;
        PackedColourFormat format;
        Vector2u size;
        
    public:
        Bitmap() : buffer(nullptr), size{0, 0}, format(Colour::packedFormatDefault)
        {}
        Bitmap(Vector2u size, PackedColourFormat format, Colour fillColour);
        Bitmap(const uint32_t* src, PackedColourFormat srcFormat, Vector2u size);

        ~Bitmap();
        Bitmap(const Bitmap& other);
        Bitmap& operator=(const Bitmap& other);
        Bitmap(Bitmap&& from);
        Bitmap& operator=(Bitmap&& from);

        bool HasData() const;
        const uint32_t* Data() const;
        Vector2u Size() const;

        void Draw(Devices::SimpleFramebuffer* fb, Gfx::Vector2u where, Gfx::Colour tint) const;
    };
}
