#pragma once

#include <NativePtr.h>

namespace np::Syscall
{
    struct MappedMemoryDetails
    {
        NativeUInt base;
        NativeUInt length;
    };
    
    struct BasicFramebufferInfo
    {
        uint16_t width;
        uint16_t height;
        uint16_t stride;
        uint16_t bpp;
        uint64_t address;
        uint8_t redOffset;
        uint8_t greenOffset;
        uint8_t blueOffset;
        uint8_t reservedOffset;
        uint8_t redMask;
        uint8_t greenMask;
        uint8_t blueMask;
        uint8_t reservedMask;
    };

    struct BasicGraphicsAdaptorInfo
    {

    };
    
    struct BasicDeviceInfo
    {
        NativeUInt deviceId;
        union 
        {
            struct
            {
                uint64_t low;
                uint64_t mid;
                uint64_t high;
            } words;
            BasicFramebufferInfo framebuffer;
            BasicGraphicsAdaptorInfo graphicsAdaptor;
        };

        BasicDeviceInfo(uint64_t id, uint64_t low, uint64_t mid, uint64_t high) : deviceId(id)
        {
            words.low = low;
            words.mid = mid;
            words.high = high;
        }
    };

    struct DetailedDeviceInfo
    {
        NativeUInt deviceId;
    };

    struct FileInfo
    {
        uint64_t fileSize;
    };

    using FileHandle = size_t;
}
