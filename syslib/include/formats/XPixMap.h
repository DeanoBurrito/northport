#pragma once

#include <stddef.h>
#include <stdint.h>
#include <containers/Vector.h>

namespace sl
{
    struct XPixMap
    {
    private:
        const char** source;
        size_t valuesStartLine;
        size_t coloursStartLine;
        size_t pixelsStartLine;

        size_t charsPerId;
        size_t coloursCount;
        size_t width;
        size_t height;

    public:
        XPixMap() : source(nullptr), valuesStartLine(0), coloursStartLine(0), pixelsStartLine(0), 
            charsPerId(0), coloursCount(0), width(0), height(0)
        {}

        XPixMap(const char* src[]);

        sl::Vector<uint32_t> GetColours() const;
        sl::Vector<size_t> GetPixels() const;

        struct Dimensions //TODO: this will leads to lots of duplicate types, lets implement a proper vector2d type
        { size_t x, y; };
        Dimensions Size() const;
    };
}
