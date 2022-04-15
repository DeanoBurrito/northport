#pragma once

#include <stddef.h>
#include <Vectors.h>
#include <String.h>

namespace WindowServer
{
    enum class WindowFlags : size_t
    {
        None = 0,
        RenderControlCluster = (1 << 0),
        RenderTitleBar = (1 << 1),
        Resizable = (1 << 2),
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
        WindowFlags flags;

        sl::String title;
    };
}
