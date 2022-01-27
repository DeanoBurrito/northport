#include <formats/XPixMap.h>
#include <StringCulture.h>

namespace sl
{
    XPixMap::XPixMap(const char* src[])
    {
        source = src;

        valuesStartLine = 0;
        size_t token = 0;
        sl::StringCulture::current->TryGetUInt64(&width, source[valuesStartLine], token);
        token = sl::memfirst(source[valuesStartLine], ' ', 0) + 1;
        sl::StringCulture::current->TryGetUInt64(&height, source[valuesStartLine], token);
        token = sl::memfirst(source[valuesStartLine], token, ' ', 0) + 1;
        sl::StringCulture::current->TryGetUInt64(&coloursCount, source[valuesStartLine], token);
        token = sl::memfirst(source[valuesStartLine], token, ' ', 0) + 1;
        sl::StringCulture::current->TryGetUInt64(&charsPerId, source[valuesStartLine], token);

        coloursStartLine = 1;
        pixelsStartLine = coloursStartLine + coloursCount;
    }

    sl::Vector<uint32_t> XPixMap::GetColours() const
    {
        sl::Vector<uint32_t> colours(coloursCount);

        for (size_t i = coloursStartLine; i < coloursCount + coloursStartLine; i++)
        {
            //NOTE: we're always assuming the type of colour entry is 'c' for colour, not any of the others.
            size_t tokenStart = sl::memfirst(source[i], '#', 0) + 1;
            uint32_t scratch;
            sl::StringCulture::current->TryGetUInt32(&scratch, source[i], tokenStart, true);
            colours.PushBack(scratch);
        }

        return colours;
    }

    sl::Vector<size_t> XPixMap::GetPixels() const
    {   
        sl::Vector<size_t> pixels(width * height);
        for (size_t y = 0; y < height; y++)
        {
            sl::NativePtr lineStart((void*)source[y + pixelsStartLine]);

            for (size_t x = 0; x < width; x++)
            {
                const char* tokenStart = lineStart.As<const char>(x * charsPerId);
                bool needsDummy = true;
                
                for (size_t i = coloursStartLine; i < coloursCount + coloursStartLine; i++)
                {
                    if (memcmp(source[i], tokenStart, charsPerId) == 0)
                    {
                        pixels.PushBack(i - coloursStartLine);
                        needsDummy = false;
                        break;
                    }
                }
                
                if (needsDummy)
                    pixels.PushBack(0);
            }
        }

        return pixels;
    }

    Vector2u XPixMap::Size() const
    {
        return { width, height };
    }
}
