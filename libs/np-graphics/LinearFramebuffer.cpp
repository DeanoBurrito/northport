#include <LinearFramebuffer.h>
#include <Syscalls.h>
#include <Memory.h>

namespace np::Graphics
{
    LinearFramebuffer* primaryFramebuffer = nullptr;
    LinearFramebuffer* LinearFramebuffer::Screen()
    {
        if (primaryFramebuffer == nullptr)
        {
            sl::Opt<np::Syscall::PrimaryFramebufferData> maybeFbData = Syscall::GetPrimaryFramebuffer();
            if (!maybeFbData)
                return nullptr;
            
            primaryFramebuffer = new LinearFramebuffer();
            primaryFramebuffer->doubleBuffered = false;
            primaryFramebuffer->backBuffer = primaryFramebuffer->frontBuffer = maybeFbData->baseAddress;
            primaryFramebuffer->stride = maybeFbData->stride;
            primaryFramebuffer->width = maybeFbData->width;
            primaryFramebuffer->height = maybeFbData->height;
            primaryFramebuffer->bitsPerPixel = maybeFbData->bpp;
        }
        
        return primaryFramebuffer;
    }

    LinearFramebuffer LinearFramebuffer::Create(size_t width, size_t height, size_t bpp, bool doubleBuffered)
    {}

    void LinearFramebuffer::Clear(Colour colour) const
    {
        
    }

    sl::Vector2u LinearFramebuffer::Size() const
    {
        return { width, height };
    }

    void LinearFramebuffer::DrawTestPattern()
    {
        sl::memsetT<uint32_t>(backBuffer.ptr, 0xFFFFFFFF, 100000);
    }

    void LinearFramebuffer::DrawPixel(sl::Vector2u where, Colour colour)
    {
        
    }

    void LinearFramebuffer::DrawPixel(sl::Vector2u where, Colour colour, FramebufferNoLockType noLock)
    {
        
    }
    
    void LinearFramebuffer::DrawHLine(sl::Vector2u begin, int length, Colour colour)
    {
        
    }

    void LinearFramebuffer::DrawHLine(sl::Vector2u begin, int length, Colour colour, FramebufferNoLockType noLock)
    {
        
    }

    void LinearFramebuffer::DrawVLine(sl::Vector2u begin, int length, Colour colour)
    {
        
    }

    void LinearFramebuffer::DrawVLine(sl::Vector2u begin, int length, Colour colour, FramebufferNoLockType noLock)
    {
        
    }
    
    void LinearFramebuffer::DrawLine(sl::Vector2u begin, sl::Vector2u end, Colour colour)
    {
        
    }

    void LinearFramebuffer::DrawLine(sl::Vector2u begin, sl::Vector2u end, Colour colour, FramebufferNoLockType noLock)
    {
        
    }
    
    void LinearFramebuffer::DrawRect(sl::Vector2u topLeft, sl::Vector2u size, Colour colour, bool filled)
    {
        
    }

    void LinearFramebuffer::DrawUsing(SimpleRenderCallback drawFunc, sl::Vector2u where, Colour colour)
    {
        
    }
}
