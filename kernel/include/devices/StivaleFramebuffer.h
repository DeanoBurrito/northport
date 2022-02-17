#pragma once

#include <devices/interfaces/GenericFramebuffer.h>

namespace Kernel::Devices
{
    /*
        Incase we can't initialize any known graphics adaptors, we'll fallback to the bootloader provided framebuffer.
        If *any* graphics adaptors are initialized in the system, this will be removed, as we dont know which device owns it,
        So we wont know if these values are still valid after re-configuring a device.
    */
    class StivaleFramebuffer : public Interfaces::GenericFramebuffer
    {
    private:
        char lock;

        sl::NativePtr linearFramebufferBase;
        size_t width;
        size_t height;
        size_t bpp;
        Interfaces::ColourFormat format;

        void Init() override;
        void Deinit() override;

    public:
        void Reset() override;
        sl::Opt<Drivers::GenericDriver*> GetDriverInstance() override;

        bool CanModeset() const override;
        void SetMode(Interfaces::FramebufferModeset& modeset) override;
        Interfaces::FramebufferModeset GetCurrentMode() const override;

        sl::Opt<sl::NativePtr> GetAddress() const override;
    };
}
