#include <formats/Qoi.h>
#include <NativePtr.h>

namespace np::Graphics
{
    Qoi::ChunkType NextChunkType(uint8_t next)
    {
        //NOTE: the 8-bit IDs have precedence over the 2-bit ones
        switch (next >> 6)
        {
        case 0b11:
        {
            if (next == 0xFE)
                return Qoi::ChunkType::Rgb;
            else if (next == 0xFF)
                return Qoi::ChunkType::Rgba;
            else
                return Qoi::ChunkType::Run;
        }
        case 0b00:
            return Qoi::ChunkType::Index;
        case 0b01:
            return Qoi::ChunkType::Diff;
        case 0b10:
            return Qoi::ChunkType::Luma;
        }

        __builtin_unreachable();
    }

    size_t HashQoiIndex(Colour c)
    {
        return ((c.r * 3) + (c.g * 5) + (c.b * 7) + (c.a * 11)) % 64;
    }
    
    GenericImage DecodeQoi(void* buffer, size_t length)
    {
        //decoder state
        const Qoi::Header* header = sl::NativePtr(buffer).As<Qoi::Header>();
        uint32_t* outputBuffer = new uint32_t[header->width * header->height];
        Colour prevPixel(0, 0, 0, 255);
        Colour indexBuffer[64];
        size_t pixelsDecoded = 0;

        sl::NativePtr current(buffer);
        current.raw += sizeof(Qoi::Header);
        for (size_t i = 0; i < 64; i++)
            indexBuffer[i] = { 0 };

        while (current.raw < length && pixelsDecoded < header->width * header->height)
        {
            const Qoi::ChunkType nextChunk = NextChunkType(*current.As<uint8_t>());
            Colour decodedPixel(0, 0, 0, 0);

            switch (nextChunk)
            {
            case Qoi::ChunkType::Rgb:
                decodedPixel.r = *current.As<uint8_t>(1);
                decodedPixel.g = *current.As<uint8_t>(2);
                decodedPixel.b = *current.As<uint8_t>(3);
                decodedPixel.a = prevPixel.a;
                current.raw += 3;
                break;

            case Qoi::ChunkType::Rgba:
                decodedPixel.r = *current.As<uint8_t>(1);
                decodedPixel.g = *current.As<uint8_t>(2);
                decodedPixel.b = *current.As<uint8_t>(3);
                decodedPixel.a = *current.As<uint8_t>(4);
                current.raw += 4;
                break;

            case Qoi::ChunkType::Index:
                decodedPixel = indexBuffer[(*current.As<uint8_t>() & 0b11'1111)];
                break;

            case Qoi::ChunkType::Diff:
            {
                uint8_t delta_r = (*current.As<uint8_t>() >> 4) & 0b11;
                uint8_t delta_g = (*current.As<uint8_t>() >> 2) & 0b11;
                uint8_t delta_b = (*current.As<uint8_t>() >> 0) & 0b11;

                decodedPixel = prevPixel;
                decodedPixel.r += delta_r - 2;
                decodedPixel.g += delta_g - 2;
                decodedPixel.b += delta_b - 2;
                decodedPixel.a = prevPixel.a;
                break;
            }

            case Qoi::ChunkType::Luma:
            {
                uint8_t delta_r = (*current.As<uint8_t>(1) >> 4) & 0b1111;
                uint8_t delta_g = (*current.As<uint8_t>() >> 2) & 0b11'1111;
                uint8_t delta_b = (*current.As<uint8_t>(1) >> 0) & 0b1111;

                decodedPixel = prevPixel;
                decodedPixel.r += (delta_g - 32) + (delta_r - 8);
                decodedPixel.g += delta_g - 32;
                decodedPixel.b += (delta_g - 32) + (delta_b - 8);
                decodedPixel.a = prevPixel.a;

                current.raw++;
                break;
            }

            case Qoi::ChunkType::Run:
            {
                //we dont apply the bias of +1 here because that will be added by the loop tail below
                //there we need to add one less pixels than run length encodes, which works out nicely.
                size_t runLength = (*current.As<uint8_t>()) & 0b11'1111;
                decodedPixel = prevPixel;

                while (runLength > 0)
                {
                    outputBuffer[pixelsDecoded] = decodedPixel.Pack(RGBA32);
                    pixelsDecoded++;
                }

                break;
            }
            }
            current.raw++;

            indexBuffer[HashQoiIndex(decodedPixel)] = decodedPixel;
            prevPixel = decodedPixel;

            outputBuffer[pixelsDecoded] = decodedPixel.Pack(RGBA32);
            pixelsDecoded++;
        }

        //TODO: check for the ending sequence (7x 0x00, then 1x 0x01).

        //this ctor takes ownership of the buffer, so its not leaked.
        return GenericImage(header->width, header->height, outputBuffer);
    }
}
