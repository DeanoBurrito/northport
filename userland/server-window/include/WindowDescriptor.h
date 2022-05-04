#pragma once

#include <stddef.h>
#include <Vectors.h>
#include <String.h>
#include <Rects.h>

namespace WindowServer
{
    constexpr size_t windowTitleHeight = 32;
    constexpr size_t windowBorderWidth = 4;
    
    enum class WindowControlFlags : size_t
    {
        None = 0,
        RenderControlCluster = (1 << 0),
        RenderTitleBar = (1 << 1),
        Resizable = (1 << 2),
    };

    enum class WindowStatusFlags : size_t
    {
        None = 0,
        NeedsRedraw = (1 << 0),
        Minimized = (1 << 1),
    };

    struct WindowDescriptor
    {
    friend class Renderer;
    private:
        size_t framebufferId;

    public:
        sl::Vector2u size;
        sl::Vector2u position;
        size_t monitorIndex;
        size_t windowId;
        WindowControlFlags controlFlags;
        WindowStatusFlags statusFlags;

        sl::String title;

        [[gnu::always_inline]] inline
        sl::UIntRect Rect() const
        { return { position.x, position.y, size.x, size.y }; }

        [[gnu::always_inline]] inline
        sl::UIntRect BorderRect() const
        { 
            return { position.x - windowBorderWidth, position.y - windowTitleHeight,
                size.x + (windowBorderWidth * 2), size.y + windowBorderWidth + windowTitleHeight};
        }
    };
}
