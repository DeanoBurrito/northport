#pragma once

#include <stddef.h>
#include <Vectors.h>
#include <String.h>
#include <Rects.h>
#include <protocols/ProtocolClient.h>

namespace WindowServer
{
    enum class WindowControlFlags : size_t
    {
        None = 0,
        Resizable = (1 << 0),
        ShowTitlebar = (1 << 2),
        Borderless = (1 << 3)
    };

    enum class WindowStatusFlags : size_t
    {
        None = 0,
        Minimized = (1 << 0),
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
        ProtocolClient owner;

        WindowDescriptor(ProtocolClient owner) : owner(owner)
        {}

        [[gnu::always_inline]] inline
        sl::UIntRect Rect() const
        { return { position.x, position.y, size.x, size.y }; }
    };
}
