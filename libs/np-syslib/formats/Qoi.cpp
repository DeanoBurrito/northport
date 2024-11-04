#include <formats/Qoi.h>
#include <Endian.h>
#include <Memory.h>

namespace sl
{
    constexpr char QoiMagic[4] = { 'q', 'o', 'i', 'f' };

    struct QoiPixel
    {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;
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

    struct QoiHeader
    {
        char magic[4];
        Be32 width;
        Be32 height;
        uint8_t channels;
        uint8_t colourspace;
    };

    struct QoiState
    {
        QoiPixel curr = { .r = 0, .g = 0, .b = 0, .a = 255 };
        QoiPixel cache[64] = { 0 };
    };

    [[gnu::always_inline]]
    inline size_t QoiHash(QoiPixel pix)
    { 
        return (pix.r * 3 + pix.g * 5 + pix.b * 7 + pix.a * 11) % 64;
    }

    Vector2u DecodeQoi(sl::Span<const char> qoi, sl::Span<uint32_t> raw, QoiDecodePixel decodeFunc)
    {
        if (qoi.Size() == 0 || decodeFunc == nullptr)
            return {};

        auto header = reinterpret_cast<const QoiHeader*>(qoi.Begin());
        if (sl::memcmp(header->magic, QoiMagic, sizeof(QoiMagic)) != 0)
            return {};

        //the header fields are big endian, use local variables so we dont pay
        //the price of conversation twice.
        const unsigned width = header->width;
        const unsigned height = header->height;
        const size_t outputLength = width * height * 4; //4 channels, I dont bother with 3-channel output
        if (raw.Size() < outputLength)
            return { width, height };

        QoiState state {};
        size_t scan = sizeof(QoiHeader); //byte we are currently processing
        size_t runRemaining = 0;

        for (size_t i = 0; i < outputLength / 4; i++)
        {
            if (runRemaining > 0)
                runRemaining--;
            else if (scan < qoi.Size())
            {
                const uint8_t byte = qoi[scan];
                if (byte == QoiOp::Rgb)
                {
                    if (scan + 3 >= qoi.Size())
                        return {};
                    state.curr.r = qoi[++scan];
                    state.curr.g = qoi[++scan];
                    state.curr.b = qoi[++scan];
                }
                else if (byte == QoiOp::Rgba)
                {
                    if (scan + 4 >= qoi.Size())
                        return {};
                    state.curr.r = qoi[++scan];
                    state.curr.g = qoi[++scan];
                    state.curr.b = qoi[++scan];
                    state.curr.a = qoi[++scan];
                }
                else
                {
                    const uint8_t upper2 = scan & 0b1100'0000;
                    switch (upper2)
                    {
                    case QoiOp::Index:
                        state.curr = state.cache[byte];
                        break;
                    case QoiOp::Diff:
                        state.curr.r += ((byte >> 4) & 3) - 2;
                        state.curr.g += ((scan >> 2) & 3) - 2;
                        state.curr.b += (scan & 3) - 2;
                        break;
                    case QoiOp::Luma:
                    {
                        if (scan + 1 >= qoi.Size())
                            return {};
                        const uint8_t extra = qoi[++scan];
                        const int deltaGreen = (scan & 0x3F) - 32;
                        state.curr.r += deltaGreen -  8 + ((extra >> 4) & 0xF);
                        state.curr.g += deltaGreen;
                        state.curr.b += deltaGreen - 8 + (extra & 0xF);
                        break;
                    }
                    case QoiOp::Run:
                        runRemaining = scan & 0x3F;
                        break;
                    }
                }
            }

            if (runRemaining == 0)
            {
                scan++;
                state.cache[QoiHash(state.curr)] = state.curr;
            }

            raw[i] = decodeFunc(state.curr.r, state.curr.g, state.curr.b, state.curr.a);
        }

        return { width, height };
    }

    size_t EncodeQoi(sl::Span<const uint32_t> source, sl::Span<char> encoded, QoiEncodePixel pixel)
    {
    }
}
