#include <LinearFramebuffer.h>
#include <SyscallFunctions.h>
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
            auto maybeDeviceData = Syscall::GetPrimaryDeviceInfo(Syscall::DeviceType::Framebuffer);
            if (!maybeDeviceData)
                return nullptr;
            Syscall::FramebufferInfo& fbInfo = maybeDeviceData->framebuffer;
            
            primaryFramebuffer = new LinearFramebuffer();
            primaryFramebuffer->buffer = fbInfo.address;
            primaryFramebuffer->stride = fbInfo.stride;
            primaryFramebuffer->width = fbInfo.width;
            primaryFramebuffer->height = fbInfo.height;
            primaryFramebuffer->bitsPerPixel = fbInfo.bpp;

            primaryFramebuffer->bufferFormat = 
            { 
                fbInfo.redOffset,
                fbInfo.greenOffset,
                fbInfo.blueOffset,
                fbInfo.reservedOffset,
                fbInfo.redMask,
                fbInfo.greenMask,
                fbInfo.blueMask,
                fbInfo.reservedMask
            };
        }
        
        return primaryFramebuffer;
    }

    LinearFramebuffer LinearFramebuffer::Create(size_t width, size_t height, size_t bpp, ColourFormat format)
    {
        LinearFramebuffer fb;
        
        fb.owningBuffer = true;
        fb.width = width;
        fb.height = height;
        fb.bitsPerPixel = bpp;
        fb.bufferFormat = format;
        fb.stride = width * (bpp / 8);
        
        fb.buffer.ptr = new uint32_t[width * height];
        
        sl::SpinlockRelease(&fb.lock);
        return fb;
    }

    LinearFramebuffer LinearFramebuffer::CreateAt(sl::NativePtr buffer, size_t width, size_t height, size_t bpp, size_t stride, ColourFormat format)
    {
        LinearFramebuffer fb;
        
        fb.buffer = buffer;
        fb.width = width;
        fb.height = height;
        fb.stride = stride;
        fb.bitsPerPixel = bpp;
        fb.bufferFormat = format;
        fb.owningBuffer = false;

        return fb;
    }

    LinearFramebuffer::~LinearFramebuffer()
    {
        sl::ScopedSpinlock scopeLock(&lock);
        
        if (owningBuffer)
        {
            delete[] buffer.As<uint8_t>();
        }
        width = height = stride = bitsPerPixel = 0;
    }

    void LinearFramebuffer::Clear(Colour colour)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        uint32_t packedColour = colour.Pack(bufferFormat);

        for (size_t i = 0; i < height; i++)
            sl::memsetT<uint32_t>(buffer.As<void>(i * stride), packedColour, width);
    }

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

    void LinearFramebuffer::CopyFrom(LinearFramebuffer& source, sl::Vector2u destPos, sl::UIntRect srcRect)
    {
        //TODO: we assume both buffers are using the same colour format here
        sl::ScopedSpinlock sourceLock(&source.lock);
        sl::ScopedSpinlock localLock(&lock);

        srcRect.left = sl::clamp(srcRect.left, 0ul, source.width);
        srcRect.top = sl::clamp(srcRect.top, 0ul, source.height);
        srcRect.width = sl::min(srcRect.width, source.width - srcRect.left);
        srcRect.width = sl::min(srcRect.width, width - destPos.x);
        srcRect.height = sl::min(srcRect.height, source.height - srcRect.top);
        srcRect.height = sl::min(srcRect.height, height - destPos.y);

        const size_t srcBytesPerPixel = source.bitsPerPixel / 8;

        for (size_t i = 0; i < srcRect.height; i++)
            sl::memcopy(
                source.buffer.As<void>((srcRect.top + i) * source.stride + (srcRect.left * srcBytesPerPixel)), 
                buffer.As<void>((i + destPos.y) * stride + (destPos.x * bitsPerPixel / 8)), 
                srcRect.width * srcBytesPerPixel
                );
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

        sl::MemWrite<uint32_t>(buffer.As<void>((where.x * bitsPerPixel / 8) + (where.y * stride)), colour.Pack(bufferFormat));
    }
    
    void LinearFramebuffer::DrawHLine(sl::Vector2u begin, int length, Colour colour)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        DrawHLine(begin, length, colour, NoLock);
    }

    void LinearFramebuffer::DrawHLine(sl::Vector2u begin, int length, Colour colour, FramebufferNoLockType)
    {
        if (begin.y >= height)
            return;
        
        size_t start = sl::clamp<size_t>(begin.x, 0, width);
        size_t end = (size_t)sl::clamp<int>((int)begin.x + length, 0, (int)width);
        if (end < start)
            sl::Swap(start, end);

        uint32_t packedColour = colour.Pack(bufferFormat);
        sl::memsetT<uint32_t>(buffer.As<void>(begin.y * stride + (start * bitsPerPixel / 8)), packedColour, end - start);
    }

    void LinearFramebuffer::DrawVLine(sl::Vector2u begin, int length, Colour colour)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        DrawVLine(begin, length, colour, NoLock);
    }

    void LinearFramebuffer::DrawVLine(sl::Vector2u begin, int length, Colour colour, FramebufferNoLockType)
    {
        if (begin.x >= width)
            return;
        
        size_t start = sl::clamp<size_t>(begin.y, 0, height);
        size_t end = (size_t)sl::clamp<int>((int)begin.y + length, 0, (int)height);
        if (end < start)
            sl::Swap(start, end);

        uint32_t packedColour = colour.Pack(bufferFormat);
        for (size_t i = start; i < end; i++)
            sl::MemWrite<uint32_t>(buffer.raw + i * stride + (begin.x * bitsPerPixel / 8), packedColour);
    }
    
    void LinearFramebuffer::DrawLine(sl::Vector2u begin, sl::Vector2u end, Colour colour)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        DrawLine(begin, end, colour, NoLock);
    }

    void LinearFramebuffer::DrawLine(sl::Vector2u begin, sl::Vector2u end, Colour colour, FramebufferNoLockType)
    {
        if (begin.y == end.y)
        {
            DrawHLine(begin, (int)end.y - (int)begin.y, colour);
            return;
        }
        if (begin.x == end.x)
        {
            DrawVLine(begin, (int)end.x - (int)begin.x, colour);
            return;
        }
        
        //This is closely based on the wikipedia pseudocode implementation of bresenham's line algorithm:
        //https://en.wikipedia.org/wiki/Bresenham%27s_line_algorithm
        
        const long derivX = sl::Labs((long)end.x - (long)begin.x);
        const long derivY = -sl::Labs((long)end.y - (long)begin.y);
        const long slopeX = begin.x < end.x ? 1 : -1;
        const long slopeY = begin.y < end.y ? 1 : -1;
        long errorValue = derivX + derivY;

        while (true)
        {
            DrawPixel(begin, colour, NoLock);
            if (begin.x == end.x && begin.y == end.y)
                return;

            const long error2 = errorValue * 2;
            if (error2 >= derivY)
            {
                if (begin.x == end.x)
                    return;
                errorValue += derivY;
                begin.x += slopeX;
            }

            if (error2 <= derivX)
            {
                if (begin.y == end.y)
                    return;
                errorValue += derivX;
                begin.y += slopeY;
            }
        }
    }
    
    void LinearFramebuffer::DrawRect(sl::UIntRect rect, Colour colour, bool filled)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        DrawRect(rect, colour, filled, NoLock);
    }

    void LinearFramebuffer::DrawRect(sl::UIntRect rect, Colour colour, bool filled, FramebufferNoLockType)
    {
        sl::Vector2u topLeft = rect.TopLeft();
        sl::Vector2u size = rect.Size();
        
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

    void LinearFramebuffer::DrawImage(const GenericImage& image, sl::Vector2u where)
    {
        sl::ScopedSpinlock scopeLock(&lock);

        for (size_t y = 0; y < image.Size().y; y++)
        {
            const size_t fbY = where.y + y;
            if (fbY >= height)
                break;
            
            for (size_t x = 0; x < image.Size().x; x++)
            {
                if ((image.Data()[y * image.Size().x + x] >> 24) < 0xFF)
                    continue; //pixel is transparent, we dont support that for now TODO:
                
                const size_t fbX = where.x + x;
                if (fbX >= width)
                    break;

                const size_t offset = (fbY * stride) + (fbX * bitsPerPixel / 8);
                sl::MemWrite<uint32_t>(buffer.raw + offset, image.Data()[y * image.Size().x + x]);
            }
        }
    }

    void LinearFramebuffer::DrawUsing(SimpleRenderCallback drawFunc, sl::Vector2u where, Colour colour)
    {
        sl::ScopedSpinlock scopeLock(&lock);

        if (drawFunc != nullptr)
            drawFunc(this, where, colour);
    }
}
