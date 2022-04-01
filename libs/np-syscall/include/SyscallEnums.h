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

        GetFileInfo = 0x30,
        OpenFile = 0x31,
        CloseFile = 0x32,
        ReadFromFile = 0x33,
        WriteToFile = 0x34,

        StartIpcStream = 0x40,
        StopIpcStream = 0x41,
        OpenIpcStream = 0x42,
        CloseIpcStream = 0x43,
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

    enum class DeviceError : NativeUInt
    {
        FeatureNotAvailable = 1,
        NoPrimaryDevice = 2,
        UnknownDeviceType = 3,
    };

    enum class FileError : NativeUInt
    {
        FileNotFound = 1,
        NoResourceId = 2,
        InvalidBufferRange = 3,
    };

    enum class IpcStreamFlags : NativeUInt
    {
        None = 0,
        //if set, buffer is zero-copy, otherwise buffer is single copy (target->dest).
        UseSharedMemory = (1 << 0),
    };

    enum class IpcError : NativeUInt
    {
        StreamStartFail = 1,
        NoResourceId = 2,
        InvalidBufferRange = 3,
    };
}
