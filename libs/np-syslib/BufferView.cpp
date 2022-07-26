#include <BufferView.h>

namespace sl
{
    bool BufferView::Contains(sl::NativePtr ptr)
    {
        return (ptr.raw >= base.raw) && ((ptr.raw - base.raw) < length);
    }

    bool BufferView::Contains(sl::BufferView range)
    {
        const uintptr_t rangeTop = range.base.raw + range.length;
        return range.base.raw >= base.raw && rangeTop < base.raw + length;
    }

    bool BufferView::Intersects(sl::BufferView range)
    {
        return range.base.raw < base.raw + length && range.base.raw + range.length > base.raw;
    }
}
