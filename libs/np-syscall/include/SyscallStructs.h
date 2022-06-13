#pragma once

#include <SyscallEnums.h>
#include <Vectors.h>
#include <Keys.h>

namespace np::Syscall
{
    struct GraphicsAdaptorInfo
    {

    };

    struct FramebufferInfo
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

    struct KeyboardInfo
    {
        uint64_t aggregateId;
    };

    struct MouseInfo
    {
        uint64_t aggregateId;
        uint16_t axisCount;
        uint16_t buttonCount;
    };
    
    struct DeviceInfo
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
            FramebufferInfo framebuffer;
            GraphicsAdaptorInfo graphicsAdaptor;
            KeyboardInfo keyboard;
            MouseInfo mouse;
        };

        DeviceInfo(uint64_t id, uint64_t low, uint64_t mid, uint64_t high) : deviceId(id)
        {
            words.low = low;
            words.mid = mid;
            words.high = high;
        }
    };

    struct FileInfo
    {
        uint64_t fileSize;
    };

    using FileHandle = size_t;
    using IpcHandle = size_t;

    struct ProgramEvent
    {
        ProgramEventType type;
        uint32_t dataLength;
        size_t handle;
    };

    using KeyboardEvent = KeyEvent;

    struct MouseEvent
    {
        sl::Vector2i cursorRelative;
    };
}
