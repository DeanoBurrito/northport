#pragma once

#include <stddef.h>
#include <Optional.h>

namespace Kernel::Devices::Interfaces
{
    struct FramebufferModeset
    {
    public:
        size_t width;
        size_t height;
        size_t bitsPerPixel;

        FramebufferModeset(size_t w, size_t h, size_t bpp) : width(w), height(h), bitsPerPixel(bpp)
        {}
    };
    
    class GenericFramebuffer
    {
    public:
        virtual ~GenericFramebuffer()
        {};

        virtual void Init() = 0;
        virtual void Destroy() = 0;
        virtual bool IsAvailable() const = 0;

        virtual bool CanModeset() const = 0;
        virtual void SetMode(FramebufferModeset& modeset) = 0;
        virtual FramebufferModeset GetCurrentMode() const = 0;

        virtual sl::Opt<sl::NativePtr> GetAddress() const = 0;
    };
}
