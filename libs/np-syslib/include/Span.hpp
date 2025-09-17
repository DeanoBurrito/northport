#pragma once

#include <Types.hpp>

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

        Span Subspan(size_t begin, size_t length) const
        {
            if (length > size || begin + length > size)
                length = size - begin;
            return Span(data + begin, length);
        }

        Span<const T> Const() const
        {
            return Span<const T>(data, size);
        }

        Span Find(Span other) const
        {
            if (other.size > size || other.Empty())
                return {};

            for (size_t i = 0; i < size - other.size; i++)
            {
                if (data[i] != other.data[0])
                    continue;
                auto test = Subspan(i, other.Size());
                if (test == other)
                    return test;
            }
            
            return {};
        }

        bool Contains(Span other) const
        {
            if (other.size > size || other.Empty())
                return false;

            for (size_t i = 0; i < size; i++)
            {
                if (data[i] != other.data[0])
                    continue;

                if (Subspan(i, other.Size()) == other)
                    return true;
            }

            return false;
        }
    };

    template<typename T>
    bool operator==(const Span<T>& a, const Span<T>& b)
    {
        if (a.Size() != b.Size())
            return false;

        for (size_t i = 0; i < a.Size(); i++)
        {
            if (a[i] != b[i])
                return false;
        }
        return true;
    }

    template<typename T>
    bool operator!=(const Span<T>& a, const Span<T>& b)
    {
        return !(a == b);
    }

    using StringSpan = Span<const char>;
}

constexpr inline sl::StringSpan operator""_span(const char* str, size_t len)
{
    return sl::StringSpan(str, len);
}

constexpr inline sl::Span<const uint8_t> operator""_u8span(const char* str, size_t len)
{
    return sl::Span<const uint8_t>((const uint8_t*)str, len);
}
