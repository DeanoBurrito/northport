#pragma once

/*
    The QOI (quite okay image) format is reasonably performant and straight forward alternative to some
    of the classic image formats (jpeg, png). It's intent it to be a far simpler (where generally faster, 
    if a little less effecient) than png. The spec is public domain and the reference implementation
    is under the MIT license. Both are linked from the website.

    Website: https://qoiformat.org/
*/

#include <formats/GenericImage.h>

namespace np::Graphics
{
    namespace Qoi
    {
        constexpr uint8_t Magic0 = 'q';
        constexpr uint8_t Magic1 = 'o';
        constexpr uint8_t Magic2 = 'i';
        constexpr uint8_t Magic3 = 'f';
        
        struct [[gnu::packed]] Header
        {
            uint8_t magic[4];
            uint32_t width;
            uint32_t height;
            uint8_t channels;
            uint8_t colourspace;
        };

        enum class ChunkType
        {
            Rgb,
            Rgba,
            Index,
            Diff,
            Luma,
            Run,
        };
    }
    
    GenericImage DecodeQoi(void* buffer, size_t length);
}
