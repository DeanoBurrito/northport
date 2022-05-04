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
        np::Graphics::LinearFramebuffer* screenFb;
        np::Graphics::LinearFramebuffer mainFb;

        GenericImage cursorImage;
        GenericImage closeImage;
        GenericImage minImage;
        GenericImage maxImage;

        void LoadFile(const sl::String& name, GenericImage& image);
        void DrawWindow(WindowDescriptor* window);

    public:
        Renderer();

        void Redraw(const sl::Vector<sl::UIntRect> damageRects, const sl::Vector<WindowDescriptor*> windows, sl::Vector2u cursor);
        sl::Vector2u Size() const;
        sl::Vector2u CursorSize() const;
    };
}
