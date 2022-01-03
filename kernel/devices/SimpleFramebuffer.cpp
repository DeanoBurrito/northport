#include <devices/SimpleFramebuffer.h>
#include <boot/Stivale2.h>
#include <Platform.h>
#include <Memory.h>
#include <Utilities.h>
#include <Maths.h>
#include <Log.h>

namespace Kernel::Devices
{
    SimpleFramebuffer simpleFramebufferInstance;
    SimpleFramebuffer* SimpleFramebuffer::Global()
    { return &simpleFramebufferInstance; }
    
    void SimpleFramebuffer::Init(stivale2_struct_tag_framebuffer* framebufferTag)
    {
        ScopedSpinlock scopeLock(&lock);
        
        baseAddress = framebufferTag->framebuffer_addr;
        width = framebufferTag->framebuffer_width;
        height = framebufferTag->framebuffer_height;
        stride = framebufferTag->framebuffer_pitch;
        bitsPerPixel = framebufferTag->framebuffer_bpp;
        
        if (framebufferTag->memory_model != 1)
        {
            Log("Framebuffer tag is broken, memory model must be 1. Unless mint snuck in a new patch.", LogSeverity::Error);
            available = false;
            return;
        }

        if (framebufferTag->red_mask_size != 8 || framebufferTag->green_mask_size != 8 || framebufferTag->blue_mask_size != 8)
        {
            Log("Unsupported framebuffer pixel config. Simple framebuffer device disable.", LogSeverity::Error);
            available = false;
            return;
        }

        uint32_t testFormat = (0xAA << framebufferTag->red_mask_shift) | 
            (0xBB << framebufferTag->green_mask_shift) | (0xCC << framebufferTag->blue_mask_shift);
        switch (testFormat) 
        {
        case 0xAABBCC00:
            nativeFormat = Gfx::PackedColourFormat::RGBr_8bpp;
            break;
        case 0xCCBBAA00:
            nativeFormat = Gfx::PackedColourFormat::BGRr_8bpp; 
            break;
        case 0x00AABBCC:
            nativeFormat = Gfx::PackedColourFormat::rRGB_8bpp;
            break;
        case 0x00CCBBAA:
            nativeFormat = Gfx::PackedColourFormat::rBGR_8bpp;
            break;
        default:
            Log("Unsupported framebuffer pixel format. Simple framebuffer device disabled.", LogSeverity::Error);
            available = false;
            break;
        }
        
        available = true;
        Logf("Framebuffer described as: w=%u, h=%u, stride=%u, bpp=%u, base=0x%x", LogSeverity::Info, width, height, stride, bitsPerPixel, baseAddress.raw);

        //this is a reasonable default for inside the kernel.
        Gfx::Colour::packedFormatDefault = nativeFormat;
    }

    void SimpleFramebuffer::Clear(Gfx::Colour clearColour)
    {
        if (!available)
            return;
        ScopedSpinlock scopeLock(&lock);
        
        for (size_t i = 0; i < height; i++)
            sl::memsetT<uint32_t>(baseAddress.As<void>(i * stride), clearColour.GetPacked(nativeFormat), width);
    }

    char* SimpleFramebuffer::GetLock()
    { return &lock; }

    void SimpleFramebuffer::DrawTestPattern()
    {
        Clear(0x1f1f1fFF);
        
        DrawRect({100, 100, 100, 100}, Gfx::Colours::Red, true);
        DrawRect({200, 100, 100, 100}, Gfx::Colours::DarkRed, true);
        DrawRect({300, 100, 100, 100}, Gfx::Colours::Green, true);
        DrawRect({400, 100, 100, 100}, Gfx::Colours::DarkGreen, true);
        DrawRect({500, 100, 100, 100}, Gfx::Colours::Blue, true);
        DrawRect({600, 100, 100, 100}, Gfx::Colours::DarkBlue, true);

        DrawRect({100, 200, 100, 100}, Gfx::Colours::Cyan, true);
        DrawRect({200, 200, 100, 100}, Gfx::Colours::DarkCyan, true);
        DrawRect({300, 200, 100, 100}, Gfx::Colours::Magenta, true);
        DrawRect({400, 200, 100, 100}, Gfx::Colours::DarkMagenta, true);
        DrawRect({500, 200, 100, 100}, Gfx::Colours::Yellow, true);
        DrawRect({600, 200, 100, 100}, Gfx::Colours::DarkYellow, true);

        DrawRect({100, 300, 100, 100}, Gfx::Colours::Red, false);
        DrawRect({200, 300, 100, 100}, Gfx::Colours::DarkRed, false);
        DrawRect({300, 300, 100, 100}, Gfx::Colours::Green, false);
        DrawRect({400, 300, 100, 100}, Gfx::Colours::DarkGreen, false);
        DrawRect({500, 300, 100, 100}, Gfx::Colours::Blue, false);
        DrawRect({600, 300, 100, 100}, Gfx::Colours::DarkBlue, false);

        DrawRect({100, 400, 100, 100}, Gfx::Colours::Cyan, false);
        DrawRect({200, 400, 100, 100}, Gfx::Colours::DarkCyan, false);
        DrawRect({300, 400, 100, 100}, Gfx::Colours::Magenta, false);
        DrawRect({400, 400, 100, 100}, Gfx::Colours::DarkMagenta, false);
        DrawRect({500, 400, 100, 100}, Gfx::Colours::Yellow, false);
        DrawRect({600, 400, 100, 100}, Gfx::Colours::DarkYellow, false);

        DrawRect({750, 150, 100, 100}, Gfx::Colours::White, true);
        DrawRect({850, 150, 100, 100}, Gfx::Colours::Black, true);
        DrawRect({750, 250, 100, 100}, Gfx::Colours::White, false);
        DrawRect({850, 250, 100, 100}, Gfx::Colours::Black, false);
    }

    Gfx::Vector2u SimpleFramebuffer::Size() const
    {
        return { width, height };
    }

    Gfx::PackedColourFormat SimpleFramebuffer::GetNativeFormat() const
    {
        return nativeFormat;
    }

    void SimpleFramebuffer::DrawPixel(Gfx::Vector2u where, Gfx::Colour colour)
    {
        ScopedSpinlock scopeLock(&lock);
        DrawPixel(where, colour, NoLock);
    }

    void SimpleFramebuffer::DrawPixel(Gfx::Vector2u where, Gfx::Colour colour, FramebufferNoLockType noLock)
    {
        (void)noLock;
        if (where.x >= width || where.y >= height || !available)
            return;
        
        *baseAddress.As<uint32_t>((where.y * stride) + (where.x * bitsPerPixel / 8)) = colour.GetPacked(nativeFormat);
    }

    void SimpleFramebuffer::DrawHLine(Gfx::Vector2u begin, int length, Gfx::Colour colour)
    {
        ScopedSpinlock scopeLock(&lock);
        DrawHLine(begin, length, colour, NoLock);
    }

    void SimpleFramebuffer::DrawHLine(Gfx::Vector2u begin, int length, Gfx::Colour colour, FramebufferNoLockType noLock)
    {
        (void)noLock;
        if (!available)
            return;
        
        size_t start = sl::clamp<size_t>(begin.x, 0, width);
        size_t end = (size_t)sl::clamp<int>((int)begin.x + length, 0, (int)width);
        if (end < start)
            sl::Swap(start, end);

        sl::memsetT<uint32_t>(baseAddress.As<void>(begin.y * stride + (start * bitsPerPixel / 8)), colour.GetPacked(nativeFormat), end - start);
    }

    void SimpleFramebuffer::DrawVLine(Gfx::Vector2u begin, int length, Gfx::Colour colour)
    {
        ScopedSpinlock scopeLock(&lock);
        DrawVLine(begin, length, colour, NoLock);
    }

    void SimpleFramebuffer::DrawVLine(Gfx::Vector2u begin, int length, Gfx::Colour colour, FramebufferNoLockType noLock)
    {
        (void)noLock;
        if (!available)
            return;

        size_t start = sl::clamp<size_t>(begin.y, 0, height);
        size_t end = (size_t)sl::clamp<int>((int)begin.y + length, 0, (int)height);
        if (end < start)
            sl::Swap(start, end);

        for (size_t i = start; i < end; i++)
            *baseAddress.As<uint32_t>(i * stride + (begin.x * bitsPerPixel / 8)) = colour.GetPacked(nativeFormat);
    }

    void SimpleFramebuffer::DrawLine(Gfx::Vector2u begin, Gfx::Vector2u end, Gfx::Colour colour)
    {
        ScopedSpinlock scopeLock(&lock);
        DrawLine(begin, end, colour, NoLock);
    }
    
    void SimpleFramebuffer::DrawLine(Gfx::Vector2u begin, Gfx::Vector2u end, Gfx::Colour colour, FramebufferNoLockType noLock)
    {
        (void)noLock;
        if (!available)
            return;
        
        (void)begin; (void)end; (void)colour;
        //TODO: implement bresenham's algorithm or similar
    }

    void SimpleFramebuffer::DrawRect(Gfx::IntRect rect, Gfx::Colour colour, bool filled)
    {
        if (!available)
            return;

        if (filled)
        {            
            for (size_t i = 0; i < (unsigned)rect.height; i++)
                DrawHLine({(unsigned)rect.left, (unsigned)rect.top + i}, rect.width, colour);
        }
        else
        {
            Gfx::Vector2u topLeft{(unsigned)rect.left, (unsigned)rect.top};
            Gfx::Vector2u botLeft{(unsigned)rect.left, (unsigned)(rect.top + rect.height - 1)};
            Gfx::Vector2u topRight{(unsigned)(rect.left + rect.width - 1), (unsigned)rect.top};

            DrawHLine(topLeft, rect.width, colour);
            DrawHLine(botLeft, rect.width, colour);
            DrawVLine(topLeft, rect.height, colour);
            DrawVLine(topRight, rect.height, colour);
        }
    }

    void SimpleFramebuffer::DrawUsing(SimpleRenderCallback drawFunc, Gfx::Vector2u where, Gfx::Colour colour)
    {
        if (!available)
            return;
        ScopedSpinlock scopeLock(&lock);

        if (drawFunc != nullptr)
            drawFunc(this, where, colour);
    }
}