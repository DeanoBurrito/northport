#pragma once

#include <stdint.h>
#include <gfx/GraphicsPrimitives.h>
#include <NativePtr.h>

//forward declaration so we dont have to include to whole stivale2.h
struct stivale2_struct_tag_framebuffer;

namespace Kernel::Devices
{
    class SimpleFramebuffer;
    using RenderCallback = void (*)(SimpleFramebuffer* framebuffer, Gfx::Vector2u where, Gfx::Colour colour);

    class SimpleFramebuffer
    {
    private:
        sl::NativePtr baseAddress;
        size_t width;
        size_t height;
        size_t stride;
        size_t bitsPerPixel;
        Gfx::PackedColourFormat nativeFormat;

        bool available;
        char lock;

    public:
        static SimpleFramebuffer* Global();

        void Init(stivale2_struct_tag_framebuffer* framebufferTag);
        void Clear(Gfx::Colour clearColor = Gfx::Colours::Black);

        void DrawTestPattern();
        Gfx::Vector2u Size() const;

        //puts a single pixel on the screen.
        void DrawPixel(Gfx::Vector2u where, Gfx::Colour colour);
        //draws a horizontal line. length can be positive or negative to indicate direction from start
        void DrawHLine(Gfx::Vector2u begin, int length, Gfx::Colour colour);
        //draws a vertical line. length can be positive or negative to indicate direction from start
        void DrawVLine(Gfx::Vector2u begin, int length, Gfx::Colour colour);
        //draws a complex line from begin to end. Check out DrawHLine()/DrawVLine() if you only need those.
        void DrawLine(Gfx::Vector2u begin, Gfx::Vector2u end, Gfx::Colour colour);
        //draws a rectangle. Filled sets whether just the outlines or the full thing are drawn.
        void DrawRect(Gfx::IntRect rect, Gfx::Colour colour, bool filled);

        //draws a complex output using the callback function, passing through the position and colour info
        void DrawUsing(RenderCallback drawFunc, Gfx::Vector2u where, Gfx::Colour colour);

        //templated function, requires T to have a Draw() function matching the RenderCallback() definition.
        template<typename T>
        void Draw(T renderable, Gfx::Vector2u where, Gfx::Colour colour)
        {
            DrawUsing(renderable.Draw, where, colour);
        }
    };

}