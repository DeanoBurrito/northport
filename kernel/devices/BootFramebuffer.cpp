#include <devices/BootFramebuffer.h>
#include <devices/DeviceManager.h>
#include <boot/Limine.h>
#include <Log.h>

namespace Kernel::Devices
{
    void BootFramebuffer::Init()
    {
        type = DeviceType::GraphicsFramebuffer;

        if (Boot::framebufferRequest.response == nullptr)
        {
            Log("Bootloader did not provide any framebuffers.", LogSeverity::Warning);
            state = DeviceState::Error;
            return;
        }

        if (Boot::framebufferRequest.response->framebuffer_count <= framebufferIndex)
        {
            Logf("Bootloader did not provide framebuffer %u, cannot initialize.", LogSeverity::Error, framebufferIndex);
            state = DeviceState::Error;
            return;
        }

        const auto bootFb = Boot::framebufferRequest.response; 
        Logf("Bootloader framebuffer %u: w=%u, h=%u, bpp=%u", LogSeverity::Info, framebufferIndex, bootFb->framebuffers[0]->width, bootFb->framebuffers[0]->height);
        linearFramebufferBase = bootFb->framebuffers[0]->address;
        width = bootFb->framebuffers[0]->width;
        height = bootFb->framebuffers[0]->height;
        bpp = bootFb->framebuffers[0]->bpp;

        uint8_t redMask = 0, greenMask = 0, blueMask = 0;
        for (size_t i = 0; i < bootFb->framebuffers[0]->red_mask_size; i++)
            redMask |= 1 << i;
        for (size_t i = 0; i < bootFb->framebuffers[0]->green_mask_size; i++)
            greenMask |= 1 << i;
        for (size_t i = 0; i < bootFb->framebuffers[0]->blue_mask_size; i++)
            blueMask |= 1 << i;
        format = { bootFb->framebuffers[0]->red_mask_shift, bootFb->framebuffers[0]->green_mask_shift, bootFb->framebuffers[0]->blue_mask_shift, 0, redMask, greenMask, blueMask, 0 };
        
        state = DeviceState::Ready;
    }

    void BootFramebuffer::Deinit()
    { 
        state = DeviceState::Shutdown;
    }

    void BootFramebuffer::Reset()
    { return; }

    sl::Opt<Drivers::GenericDriver*> BootFramebuffer::GetDriverInstance()
    { return {}; }

    bool BootFramebuffer::CanModeset() const
    { return false; }

    void BootFramebuffer::SetMode(Interfaces::FramebufferModeset&)
    { return; }

    Interfaces::FramebufferModeset BootFramebuffer::GetCurrentMode() const
    { return { width, height, bpp, format}; }

    sl::Opt<sl::NativePtr> BootFramebuffer::GetAddress() const
    { return linearFramebufferBase; }

    void RegisterBootFramebuffers()
    {
        if (Boot::framebufferRequest.response == nullptr)
            return;
        
        auto fbs = Boot::framebufferRequest.response;
        for (size_t i = 0; i < fbs->framebuffer_count; i++)
        {
            size_t devId = DeviceManager::Global()->RegisterDevice(new BootFramebuffer(i));
            if (i == 0)
                DeviceManager::Global()->SetPrimaryDevice(DeviceType::GraphicsFramebuffer, devId);
        }
    }
}
