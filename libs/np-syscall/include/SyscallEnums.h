#pragma once

#include <NativePtr.h>

namespace np::Syscall
{
    constexpr NativeUInt SyscallSuccess = 0;
    constexpr NativeUInt SyscallNotFound = 0x404;
    
    enum class SyscallId : NativeUInt
    {
        LoopbackTest = 0x0,

        MapMemory = 0x10,
        UnmapMemory = 0x11,
        ModifyMemoryFlags = 0x12,
        MemoryMapFile = 0x13,
        UnmapFile = 0x14,
        FlushMappedFile = 0x15,

        GetPrimaryDeviceInfo = 0x20,
        GetDevicesOfType = 0x21,
        GetDeviceInfo = 0x22,
    };

    enum class MemoryMapFlags : NativeUInt
    {
        Writable = (1 << 0),
        Executable = (1 << 1),
        UserVisible = (1 << 2),
    };

    enum class DeviceType : NativeUInt
    {
        Framebuffer = 0,
        GraphicsAdaptor = 1,
    };

    enum class GetDeviceError : NativeUInt
    {
        FeatureNotAvailable = 1,
        NoPrimaryDevice = 2,
        UnknownDeviceType = 3,
    };
}
