#pragma once

#include <stddef.h>

namespace sl
{
    template<typename T>
    class Span
    {
    private:
        T* data;
        size_t size;

    public:
        constexpr Span() : data(nullptr), size(0)
        {}

        template<size_t Count> //thanks thom_tl for this one (see github.com/thom_tl/luna)
        constexpr Span(T(&array)[Count]) : data(array), size(Count)
        {}

        constexpr Span(T* array, size_t count) : data(array), size(count)
        {}

        T* Begin()
        { return data; }

        T* End()
        { return data + size; }

        const T* Begin() const
        { return data; }

        const T* End() const
        { return data + size; }

        T& operator[](size_t index)
        { return data[index]; }

        const T& operator[](size_t index) const
        { return data[index]; }

        size_t Size() const
        { return size; }

        size_t SizeBytes() const
        { return size * sizeof(T); }

        bool Empty() const
        { return size == 0; }
    };

    using StringSpan = Span<const char>;
}
