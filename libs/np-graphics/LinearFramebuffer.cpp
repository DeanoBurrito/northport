#include <LinearFramebuffer.h>
#include <Syscalls.h>
#include <Memory.h>
#include <Locks.h>
#include <Maths.h>

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

            primaryFramebuffer->bufferFormat = 
            { 
                maybeFbData->format.redOffset,
                maybeFbData->format.greenOffset,
                maybeFbData->format.blueOffset,
                maybeFbData->format.reserved0,
                maybeFbData->format.redMask,
                maybeFbData->format.greenMask,
                maybeFbData->format.blueMask,
                maybeFbData->format.reserved1
            };
        }
        
        return primaryFramebuffer;
    }

    LinearFramebuffer LinearFramebuffer::Create(size_t width, size_t height, size_t bpp, bool doubleBuffered, ColourFormat format)
    {
        LinearFramebuffer fb;
        
        //TODO: cache hinting in memmaps, specifically write-combine
        //TODO: implement LinearFramebuffer::Create()

        return fb;
    }

    void LinearFramebuffer::Clear(Colour colour)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        uint32_t packedColour = colour.Pack(bufferFormat);

        for (size_t i = 0; i < height; i++)
            sl::memsetT<uint32_t>(backBuffer.As<void>(i * stride), packedColour, width);
    }

    void LinearFramebuffer::SwapBuffers()
    {}

    void LinearFramebuffer::SetBufferFormat(const ColourFormat& format)
    { 
        sl::ScopedSpinlock scopeLock(&lock); //we dont want to change framebuffer format mid-operation
        bufferFormat = format; 
    }

    ColourFormat LinearFramebuffer::GetBufferFormat() const
    { return bufferFormat; }

    sl::Vector2u LinearFramebuffer::Size() const
    {
        return { width, height };
    }

    char* LinearFramebuffer::GetLock()
    { return &lock; }

    void LinearFramebuffer::DrawTestPattern()
    {
        Clear(0x1f1f1fFF);
        
        DrawRect({ 100, 100, 100, 100 }, Colours::Red, true);
        DrawRect({ 200, 100, 100, 100 }, Colours::DarkRed, true);
        DrawRect({ 300, 100, 100, 100 }, Colours::Green, true);
        DrawRect({ 400, 100, 100, 100 }, Colours::DarkGreen, true);
        DrawRect({ 500, 100, 100, 100 }, Colours::Blue, true);
        DrawRect({ 600, 100, 100, 100 }, Colours::DarkBlue, true);

        DrawRect({ 100, 200, 100, 100 }, Colours::Cyan, true);
        DrawRect({ 200, 200, 100, 100 }, Colours::DarkCyan, true);
        DrawRect({ 300, 200, 100, 100 }, Colours::Magenta, true);
        DrawRect({ 400, 200, 100, 100 }, Colours::DarkMagenta, true);
        DrawRect({ 500, 200, 100, 100 }, Colours::Yellow, true);
        DrawRect({ 600, 200, 100, 100 }, Colours::DarkYellow, true);

        DrawRect({ 100, 300, 100, 100 }, Colours::Red, false);
        DrawRect({ 200, 300, 100, 100 }, Colours::DarkRed, false);
        DrawRect({ 300, 300, 100, 100 }, Colours::Green, false);
        DrawRect({ 400, 300, 100, 100 }, Colours::DarkGreen, false);
        DrawRect({ 500, 300, 100, 100 }, Colours::Blue, false);
        DrawRect({ 600, 300, 100, 100 }, Colours::DarkBlue, false);

        DrawRect({ 100, 400, 100, 100 }, Colours::Cyan, false);
        DrawRect({ 200, 400, 100, 100 }, Colours::DarkCyan, false);
        DrawRect({ 300, 400, 100, 100 }, Colours::Magenta, false);
        DrawRect({ 400, 400, 100, 100 }, Colours::DarkMagenta, false);
        DrawRect({ 500, 400, 100, 100 }, Colours::Yellow, false);
        DrawRect({ 600, 400, 100, 100 }, Colours::DarkYellow, false);

        DrawRect({ 750, 150, 100, 100 }, Colours::White, true);
        DrawRect({ 850, 150, 100, 100 }, Colours::Black, true);
        DrawRect({ 750, 250, 100, 100 }, Colours::White, false);
        DrawRect({ 850, 250, 100, 100 }, Colours::Black, false);
    }

    void LinearFramebuffer::DrawPixel(sl::Vector2u where, Colour colour)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        DrawPixel(where, colour, NoLock);
    }

    void LinearFramebuffer::DrawPixel(sl::Vector2u where, Colour colour, FramebufferNoLockType)
    {
        if (where.x >= width || where.y >= height)
            return;

        sl::MemWrite<uint32_t>(backBuffer.As<void>((where.x * bitsPerPixel / 8) + (where.y * stride)), colour.Pack(bufferFormat));
    }
    
    void LinearFramebuffer::DrawHLine(sl::Vector2u begin, int length, Colour colour)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        DrawHLine(begin, length, colour, NoLock);
    }

    void LinearFramebuffer::DrawHLine(sl::Vector2u begin, int length, Colour colour, FramebufferNoLockType)
    {
        size_t start = sl::clamp<size_t>(begin.x, 0, width);
        size_t end = (size_t)sl::clamp<int>((int)begin.x + length, 0, (int)width);
        if (end < start)
            sl::Swap(start, end);

        uint32_t packedColour = colour.Pack(bufferFormat);
        sl::memsetT<uint32_t>(backBuffer.As<void>(begin.y * stride + (start * bitsPerPixel / 8)), packedColour, end - start);
    }

    void LinearFramebuffer::DrawVLine(sl::Vector2u begin, int length, Colour colour)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        DrawVLine(begin, length, colour, NoLock);
    }

    void LinearFramebuffer::DrawVLine(sl::Vector2u begin, int length, Colour colour, FramebufferNoLockType)
    {
        size_t start = sl::clamp<size_t>(begin.y, 0, height);
        size_t end = (size_t)sl::clamp<int>((int)begin.y + length, 0, (int)height);
        if (end < start)
            sl::Swap(start, end);

        uint32_t packedColour = colour.Pack(bufferFormat);
        for (size_t i = start; i < end; i++)
            sl::MemWrite<uint32_t>(backBuffer.raw + i * stride + (begin.x * bitsPerPixel / 8), packedColour);
    }
    
    void LinearFramebuffer::DrawLine(sl::Vector2u begin, sl::Vector2u end, Colour colour)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        DrawLine(begin, end, colour, NoLock);
    }

    void LinearFramebuffer::DrawLine(sl::Vector2u begin, sl::Vector2u end, Colour colour, FramebufferNoLockType)
    {
        //TODO: draw line implementation
    }
    
    void LinearFramebuffer::DrawRect(sl::UIntRect rect, Colour colour, bool filled)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        DrawRect(rect, colour, filled, NoLock);
    }

    void LinearFramebuffer::DrawRect(sl::UIntRect rect, Colour colour, bool filled, FramebufferNoLockType)
    {
        sl::Vector2u topLeft { rect.left, rect.top };
        sl::Vector2u size { rect.width, rect.height };
        
        if (filled)
        {
            for (size_t i = 0; i < size.y; i++)
                DrawHLine({ topLeft.x, topLeft.y + i}, (signed)size.x, colour, NoLock);
        }
        else
        {
            sl::Vector2u topRight = { topLeft.x + size.x - 1, topLeft.y };
            sl::Vector2u botLeft = { topLeft.x, topLeft.y + size.y - 1 };

            DrawHLine(topLeft, (signed)size.x, colour, NoLock);
            DrawHLine(botLeft, (signed)size.x, colour, NoLock);
            DrawVLine(topLeft, (signed)size.y, colour, NoLock);
            DrawVLine(topRight, (signed)size.y, colour, NoLock);
        }
    }

    void LinearFramebuffer::DrawUsing(SimpleRenderCallback drawFunc, sl::Vector2u where, Colour colour)
    {
        sl::ScopedSpinlock scopeLock(&lock);

        if (drawFunc != nullptr)
            drawFunc(this, where, colour);
    }
}
