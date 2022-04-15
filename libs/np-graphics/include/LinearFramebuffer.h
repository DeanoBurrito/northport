#pragma once

#include <stddef.h>
#include <Colour.h>
#include <NativePtr.h>
#include <Vectors.h>
#include <Rects.h>
#include <formats/GenericImage.h>

namespace np::Graphics
{
    struct FramebufferNoLockType {};
    constexpr static inline FramebufferNoLockType NoLock = FramebufferNoLockType();

    class LinearFramebuffer;
    using SimpleRenderCallback = void (*)(LinearFramebuffer* framebuffer, sl::Vector2u where, Colour colour);
    
    class LinearFramebuffer
    {
    private:
        sl::NativePtr backBuffer;
        sl::NativePtr frontBuffer;
        size_t stride;
        size_t width;
        size_t height;
        size_t bitsPerPixel;
        ColourFormat bufferFormat;
        
        char lock;
        bool doubleBuffered;

        LinearFramebuffer() = default;

    public:
        static LinearFramebuffer* Screen();
        static LinearFramebuffer Create(size_t width, size_t height, size_t bpp, bool doubleBuffered, ColourFormat format);
        static LinearFramebuffer CreateAt(sl::NativePtr frontBuffer, sl::NativePtr backBuffer, size_t width, size_t height, size_t bpp, size_t stride, ColourFormat format);

        void Clear(Colour col = Colours::Black);
        void SwapBuffers();
        void SetBufferFormat(const ColourFormat& format);
        ColourFormat GetBufferFormat() const;
        sl::Vector2u Size() const;

        char* GetLock();

        void DrawTestPattern();

        //puts a single pixel on the screen.
        void DrawPixel(sl::Vector2u where, Colour colour);
        void DrawPixel(sl::Vector2u where, Colour colour, FramebufferNoLockType noLock);
        //draws a horizontal line. length can be positive or negative to indicate direction from start
        void DrawHLine(sl::Vector2u begin, int length, Colour colour);
        void DrawHLine(sl::Vector2u begin, int length, Colour colour, FramebufferNoLockType noLock);
        //draws a vertical line. length can be positive or negative to indicate direction from start
        void DrawVLine(sl::Vector2u begin, int length, Colour colour);
        void DrawVLine(sl::Vector2u begin, int length, Colour colour, FramebufferNoLockType noLock);
        //draws a complex line from begin to end. Check out DrawHLine()/DrawVLine() if you only need those.
        void DrawLine(sl::Vector2u begin, sl::Vector2u end, Colour colour);
        void DrawLine(sl::Vector2u begin, sl::Vector2u end, Colour colour, FramebufferNoLockType noLock);
        //draws a rectangle. Filled sets whether just the outlines or the full thing are drawn.
        void DrawRect(sl::UIntRect rect, Colour colour, bool filled);
        void DrawRect(sl::UIntRect rect, Colour colour, bool filled, FramebufferNoLockType noLock);
        //draws an image onto the framebuffer
        void DrawImage(const GenericImage& image, sl::Vector2u where);

        //draws a complex output using the callback function, passing through the position and colour info
        void DrawUsing(SimpleRenderCallback drawFunc, sl::Vector2u where, Colour colour);

        //templated function, requires T to have a Draw() function matching the RenderCallback() definition.
        template<typename T>
        void Draw(T renderable, sl::Vector2u where, Colour colour)
        {
            DrawUsing(renderable.Draw, where, colour);
        }
    };
}
