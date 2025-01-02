#pragma once

#include <Span.h>
#include <Vectors.h>

/* QOI is a very simple compressed image format, not quite as good as png
 * but far less complex in the requried logic.
 * See `https://github.com/phoboslab/qoi` for specifics.
 */

namespace sl
{
    using QoiDecodePixel = uint32_t (*)(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    using QoiEncodePixel = void (*)(uint32_t pixel, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a);

    //returns the size of the raw image data in bytes, returns 0 if data is malformed.
    //decodeFunc is used to store each pixel into the buffer, so the user chooses
    //the raw format.
    Vector2u DecodeQoi(sl::Span<const char> qoi, sl::Span<uint32_t> raw, QoiDecodePixel decodeFunc);

    //returns size of encoded image, even if encoded is nullptr - can be used to check size
    //required to encode an image, then called again with a valid buffer.
    //The encodeFunc is used to extract pixels from the source buffer.
    size_t EncodeQoi(sl::Span<const uint32_t> source, sl::Span<char> encoded, QoiEncodePixel encodeFunc);
}
