#pragma once

#include <Rects.h>
#include <containers/Vector.h>
#include <LinearFramebuffer.h>
#include <WindowDescriptor.h>
#include <formats/GenericImage.h>

namespace WindowServer
{
    using np::Graphics::GenericImage;
    
    class Renderer
    {
    private:
        sl::Vector<sl::UIntRect> invalidRects;
        np::Graphics::LinearFramebuffer* outputFb;
        bool renderDebugOverlay;

        GenericImage cursorImage;
        GenericImage closeImage;
        GenericImage minImage;
        GenericImage maxImage;

        void LoadFile(const sl::String& name, GenericImage& image);
        void DrawWindow(WindowDescriptor* window);
        void DrawCursor(sl::Vector2u where);

    public:
        Renderer();

        void DrawAll(const sl::Vector<WindowDescriptor*> windows);
    };
}
