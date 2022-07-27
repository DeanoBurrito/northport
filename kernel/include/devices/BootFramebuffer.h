#pragma once

#include <devices/interfaces/GenericFramebuffer.h>

namespace Kernel::Devices
{
    class BootFramebuffer : public GenericFramebuffer
    {
    private:
        size_t framebufferIndex;

        sl::NativePtr linearFramebufferBase;
        size_t width;
        size_t height;
        size_t bpp;
        ColourFormat format;

        void Init() override;
        void Deinit() override;

    public:
        BootFramebuffer(size_t fbIndex) : framebufferIndex(fbIndex) 
        {}

        void Reset() override;
        sl::Opt<Drivers::GenericDriver*> GetDriverInstance() override;

        bool CanModeset() const override;
        void SetMode(FramebufferModeset& modeset) override;
        FramebufferModeset GetCurrentMode() const override;

        sl::Opt<sl::NativePtr> GetAddress() const override;
    };

    void RegisterBootFramebuffers();
}
