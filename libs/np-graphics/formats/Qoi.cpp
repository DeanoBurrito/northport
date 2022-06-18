#include <formats/Qoi.h>
#include <NativePtr.h>
#include <Memory.h>
#include <Maths.h>

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
    
    sl::Opt<GenericImage> DecodeQoi(sl::BufferView buffer)
    {
        //qoi is big-endian, so we may need to swap byte order
        const Qoi::Header* header = buffer.base.As<Qoi::Header>();
        const size_t width = sl::IsLittleEndian() ? sl::SwapEndianness(header->width) : header->width;
        const size_t height = sl::IsLittleEndian() ? sl::SwapEndianness(header->height) : header->height;

        //decoder state
        uint32_t* outputBuffer = new uint32_t[width * height];
        Colour workingPixel(0, 0, 0, 0xFF);
        Colour indexBuffer[64];
        size_t pixelsDecoded = 0;

        sl::NativePtr current = buffer.base;
        current.raw += sizeof(Qoi::Header);
        sl::memset(indexBuffer, 0, 256);

        while (pixelsDecoded < width * height && current.raw < buffer.base.raw + buffer.length)
        {
            const uint8_t byte = *current.As<uint8_t>();
            const Qoi::ChunkType nextChunk = NextChunkType(byte);

            switch (nextChunk)
            {
            case Qoi::ChunkType::Rgb:
                workingPixel.r = *current.As<uint8_t>(1);
                workingPixel.g = *current.As<uint8_t>(2);
                workingPixel.b = *current.As<uint8_t>(3);
                current.raw += 3;
                break;

            case Qoi::ChunkType::Rgba:
                workingPixel.r = *current.As<uint8_t>(1);
                workingPixel.g = *current.As<uint8_t>(2);
                workingPixel.b = *current.As<uint8_t>(3);
                workingPixel.a = *current.As<uint8_t>(4);
                current.raw += 4;
                break;

            case Qoi::ChunkType::Index:
                workingPixel = indexBuffer[(byte & 0b11'1111)];
                break;

            case Qoi::ChunkType::Diff:
            {
                workingPixel.r += (byte >> 4) & 0b11;
                workingPixel.g += (byte >> 2) & 0b11;
                workingPixel.b += (byte >> 0) & 0b11;
                break;
            }

            case Qoi::ChunkType::Luma:
            {
                uint8_t delta_r = (*current.As<uint8_t>(1) >> 4) & 0b1111;
                uint8_t delta_g = (*current.As<uint8_t>() >> 2) & 0b11'1111;
                uint8_t delta_b = (*current.As<uint8_t>(1) >> 0) & 0b1111;

                workingPixel.r += (delta_g - 32) + (delta_r - 8);
                workingPixel.g += delta_g - 32;
                workingPixel.b += (delta_g - 32) + (delta_b - 8);

                current.raw++;
                break;
            }

            case Qoi::ChunkType::Run:
            {
                //we dont apply the bias of +1 here because that will be added by the loop tail below
                //there we need to add one less pixels than run length encodes, which works out nicely.
                size_t runLength = byte & 0b11'1111;
                while (runLength > 0)
                {
                    outputBuffer[pixelsDecoded] = workingPixel.Pack(RGBA32);
                    pixelsDecoded++;
                    runLength--;
                }

                break;
            }
            }
            current.raw++;

            indexBuffer[HashQoiIndex(workingPixel)] = workingPixel;

            outputBuffer[pixelsDecoded] = workingPixel.Pack(RGBA32);
            pixelsDecoded++;
        }

        if (current.raw + 8 != buffer.base.raw + buffer.length)
            return {};
        for (size_t i = 0; i < 7; i++)
        {
            if (*current.As<uint8_t>() != 0)
                return {};
            current.raw++;
        }
        if (*current.As<uint8_t>() != 1)
            return {};

        //this ctor takes ownership of the buffer, so its not leaked.
        return GenericImage(width, height, outputBuffer);
    }
}
