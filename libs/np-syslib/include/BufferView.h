#pragma once

#include <NativePtr.h>

namespace sl
{
    /*
        Represents a view *into* a buffer. It owns no memory, and will not free and anything.
    */
    struct BufferView
    {
    public:
        sl::NativePtr base;
        size_t length;

        BufferView() : base(nullptr), length(0)
        {}

        BufferView(sl::NativePtr base, size_t len) : base(base), length(len)
        {}

        bool Contains(sl::NativePtr ptr);
        bool Contains(sl::BufferView range);
        bool Intersects(sl::BufferView range);
    };
}
