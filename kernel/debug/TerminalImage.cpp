/*
    The OpenImage() function uses code derived from the reference qoi decoder 
    found at https://github.com/phoboslab/qoi. As required, license text is below.

    MIT License

    Copyright (c) 2021, Dominic Szablewski - https://phoboslab.org

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/

#include <debug/TerminalImage.h>
#include <Memory.h>
#include <Maths.h>

struct [[gnu::packed]] QoiHeader
{
    uint8_t magic[4]; //"qoif"
    uint32_t width;
    uint32_t height;
    uint8_t channels;
    uint8_t colourspace;
};

union QoiPixel
{
    uint32_t squish;
    struct { uint8_t r, g, b, a; };
};

enum QoiOp : uint8_t
{
    Index = 0b0000'0000,
    Diff  = 0b0100'0000,
    Luma  = 0b1000'0000,
    Run   = 0b1100'0000,
    Rgb   = 0b1111'1110,
    Rgba  = 0b1111'1111,

    Mask2 = 0b1100'0000,
};

[[gnu::always_inline]]
inline size_t QoiHash(QoiPixel pix)
{ 
    return (pix.r * 3 + pix.g * 5 + pix.b * 7 + pix.a * 11) % 64;
}

bool OpenImage(GTImage& image, uintptr_t file, size_t size)
{
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(file);
    const QoiHeader* header = reinterpret_cast<const QoiHeader*>(bytes);
    if (sl::memcmp(header->magic, "qoif", 4) != 0)
        return false;

    const size_t width = sl::ByteSwap(header->width);
    const size_t height = sl::ByteSwap(header->height);

    const size_t pixelBufferSize = width * height * 4; //4 channels
    bytes += sizeof(QoiHeader);

    QoiPixel pix { .r = 0, .g = 0, .b = 0, .a = 255 };
    QoiPixel pixelCache[64];
    sl::memset(pixelCache, 0, sizeof(pixelCache));
    uint8_t* pixels = new uint8_t[pixelBufferSize];
    sl::memset(pixels, 0, pixelBufferSize);

    size_t runRemaining = 0;
    size_t bytesIndex = 0;
    const size_t inputLength = size - sizeof(QoiHeader);
    for (size_t i = 0; i < pixelBufferSize; i += 4)
    {
        const uint8_t scan = bytes[bytesIndex];
        if (runRemaining > 0)
            runRemaining--;
        else if (bytesIndex >= inputLength)
            goto NoParse;
        else if (scan == QoiOp::Rgb)
        {
            pix.r = bytes[++bytesIndex];
            pix.g = bytes[++bytesIndex];
            pix.b = bytes[++bytesIndex];
        }
        else if (scan == QoiOp::Rgba)
        {
            pix.r = bytes[++bytesIndex];
            pix.g = bytes[++bytesIndex];
            pix.b = bytes[++bytesIndex];
            pix.a = bytes[++bytesIndex];
        }
        else
        {
            const uint8_t upper2 = scan & Mask2;
            switch (upper2)
            {
            case QoiOp::Index:
                pix = pixelCache[scan];
                break;
            case QoiOp::Diff:
                pix.r += ((scan >> 4) & 0x3) - 2;
                pix.g += ((scan >> 2) & 0x3) - 2;
                pix.b += (scan & 0x3) - 2;
                break;
            case QoiOp::Luma:
            {
                const uint8_t extra = bytes[++bytesIndex];
                const int deltaGreen = (scan & 0x3F) - 32;
                pix.r += deltaGreen - 8 + ((extra >> 4) & 0xF);
                pix.g += deltaGreen;
                pix.b += deltaGreen - 8 + (extra & 0xF);
                break;
            }
            case QoiOp::Run:
                runRemaining = scan & 0x3F;
                break;
            }
        }

        if (runRemaining == 0)
        {
            bytesIndex++;
            pixelCache[QoiHash(pix)] = pix;
        }
NoParse:
        pixels[i + 0] = pix.b;
        pixels[i + 1] = pix.g;
        pixels[i + 2] = pix.r;
        pixels[i + 3] = pix.a;
    }

    image.width = width;
    image.height = height;
    image.bpp = 32;
    image.stride = width * 4;
    image.data = pixels;
    return true;
}

void CloseImage(GTImage& image)
{
    operator delete[](image.data, (size_t)0);
    image.data = nullptr;
}
