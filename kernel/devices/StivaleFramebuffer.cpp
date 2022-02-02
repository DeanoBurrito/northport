#include <devices/StivaleFramebuffer.h>
#include <boot/Stivale2.h>
#include <Log.h>

namespace Kernel
{
    //there are defined in KernelMain.cpp - cheeky little hack here
    extern stivale2_struct* stivale2Struct;
    stivale2_tag* FindStivaleTagInternal(uint64_t id);
}

namespace Kernel::Devices
{
    void StivaleFramebuffer::Init()
    {
        type = DeviceType::GraphicsFramebuffer;

        if (stivale2Struct == nullptr)
        {
            Log("Stivale2 struct not available, not creating stivale2 framebuffer instance.", LogSeverity::Info);
            state = DeviceState::Error;
            return;
        }

        stivale2_struct_tag_framebuffer* stivaleFb = reinterpret_cast<stivale2_struct_tag_framebuffer*>(FindStivaleTagInternal(STIVALE2_STRUCT_TAG_FRAMEBUFFER_ID));
        linearFramebufferBase = stivaleFb->framebuffer_addr;
        width = stivaleFb->framebuffer_width;
        height = stivaleFb->framebuffer_height;
        bpp = stivaleFb->framebuffer_bpp;

        uint8_t redMask = 0, greenMask = 0, blueMask = 0;
        for (size_t i = 0; i < stivaleFb->red_mask_size; i++)
            redMask |= 1 << i;
        for (size_t i = 0; i < stivaleFb->green_mask_size; i++)
            greenMask |= 1 << i;
        for (size_t i = 0; i < stivaleFb->blue_mask_size; i++)
            blueMask |= 1 << i;
        format = { stivaleFb->red_mask_shift, stivaleFb->green_mask_shift, stivaleFb->blue_mask_shift, 0, redMask, greenMask, blueMask, 0 };
        
        state = DeviceState::Ready;
    }

    void StivaleFramebuffer::Deinit()
    { return; }

    void StivaleFramebuffer::Reset()
    { return; }

    sl::Opt<void*> StivaleFramebuffer::GetDriverInstance()
    { return {}; }

    bool StivaleFramebuffer::CanModeset() const
    { return false; }

    void StivaleFramebuffer::SetMode(Interfaces::FramebufferModeset& modeset)
    { return; }

    Interfaces::FramebufferModeset StivaleFramebuffer::GetCurrentMode() const
    { return { width, height, bpp, format}; }

    sl::Opt<sl::NativePtr> StivaleFramebuffer::GetAddress() const
    { return linearFramebufferBase; }
}
