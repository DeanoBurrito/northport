#pragma once

#include <Rects.h>
#include <containers/Vector.h>
#include <LinearFramebuffer.h>
#include <WindowDescriptor.h>
#include <formats/GenericImage.h>

namespace WindowServer
{
    enum class RenderDebugDrawLevel
    {
        None,
        TextOnly,
        DamageRectsOnly,
        TextAndDamageRects,
    };

    using np::Graphics::GenericImage;
    struct DecorationConfig
    {
        size_t titleHeight;
        size_t borderWidth;

        GenericImage closeImage;
        GenericImage minImage;
        GenericImage maxImage;
    };
    
    class Renderer
    {
    private:
        np::Graphics::LinearFramebuffer* screenFb;
        np::Graphics::LinearFramebuffer mainFb;

        DecorationConfig decorConfig;
        GenericImage cursorImage;

        void LoadFile(const sl::String& name, GenericImage& image);
        void DrawWindow(WindowDescriptor* window, const sl::UIntRect& rect);

    public:
        static sl::UIntRect WindowBorderRect(const WindowDescriptor& window, const DecorationConfig& config);

        RenderDebugDrawLevel debugDrawLevel;

        Renderer();

        void Redraw(const sl::Vector<sl::UIntRect>& damageRects, const sl::Vector<WindowDescriptor*>& windows, sl::Vector2u cursor);
        sl::Vector2u Size() const;
        sl::Vector2u CursorSize() const;
        const DecorationConfig& GetDecorConfig() const;

        void AttachFramebuffer(WindowDescriptor* window);
        void DeteachFramebuffer(WindowDescriptor* window);
    };
}
