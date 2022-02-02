#pragma once

#include <stddef.h>
#include <Optional.h>
#include <devices/GenericDevice.h>
#include <Colour.h>

namespace Kernel::Devices::Interfaces
{
    using ColourFormat = np::Graphics::ColourFormat;
    
    struct FramebufferModeset
    {
    public:
        size_t width;
        size_t height;
        size_t bitsPerPixel;
        ColourFormat pixelFormat;

        FramebufferModeset(size_t w, size_t h, size_t bpp, ColourFormat format) : width(w), height(h), bitsPerPixel(bpp), pixelFormat(format)
        {}
    };
    
    class GenericFramebuffer : public GenericDevice
    {
    protected: 
        virtual void Init() = 0;
        virtual void Deinit() = 0;
        
    public:
        virtual ~GenericFramebuffer() = default;

        virtual void Reset() = 0;
        virtual sl::Opt<void*> GetDriverInstance() = 0;

        virtual bool CanModeset() const = 0;
        virtual void SetMode(FramebufferModeset& modeset) = 0;
        virtual FramebufferModeset GetCurrentMode() const = 0;

        virtual sl::Opt<sl::NativePtr> GetAddress() const = 0;
    };
}
