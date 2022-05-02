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

        GetPrimaryDeviceInfo = 0x20,
        GetDevicesOfType = 0x21,
        GetDeviceInfo = 0x22,

        GetFileInfo = 0x30,
        OpenFile = 0x31,
        CloseFile = 0x32,
        ReadFromFile = 0x33,
        WriteToFile = 0x34,
        FlushFile = 0x35,

        StartIpcStream = 0x40,
        StopIpcStream = 0x41,
        OpenIpcStream = 0x42,
        CloseIpcStream = 0x43,
        ReadFromIpcStream = 0x44,
        WriteToIpcStream = 0x45,
        CreateMailbox = 0x46,
        DestroyMailbox = 0x47,
        PostToMailbox = 0x48,
        ModifyIpcConfig = 0x49,

        Log = 0x50,

        PeekNextEvent = 0x60,
        ConsumeNextEvent = 0x61,
        GetPendingEventCount = 0x62,
    };

    enum class MemoryMapFlags : NativeUInt
    {
        Writable = (1 << 0),
        Executable = (1 << 1),
        UserVisible = (1 << 2),
    };

    enum class DeviceType : NativeUInt
    {
        GraphicsAdaptor = 0,
        Framebuffer = 1,
        Keyboard = 2,
        Mouse = 3,
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

        AccessNone = (0ul << 60),
        AccessPublic = (1ul << 60),
        AccessSelectedOnly = (2ul << 60),
        AccessPrivate = (3ul << 60),
    };

    using IpcMailboxFlags = IpcStreamFlags;

    [[gnu::always_inline]] inline
    IpcStreamFlags operator|(const IpcStreamFlags& a, const IpcStreamFlags& b)
    { return (IpcStreamFlags)((size_t)a | (size_t)b); }

    enum class IpcError : NativeUInt
    {
        StreamStartFail = 1,
        NoResourceId = 2,
        InvalidBufferRange = 3,
        MailDeliveryFailed = 4,
    };

    enum class IpcConfigOperation : NativeUInt
    {
        AddAccessId = 1,
        RemoveAccessId = 2,
        ChangeAccessFlags = 3,
        TransferOwnership = 4,
    };

    enum class LogLevel : NativeUInt
    {
        Info = 0,
        Warning = 1,
        Error = 2,
        //Fatal is not available to userspace, hopefully for obvious reasons.
        Verbose = 4,
    };

    enum class LogError : NativeUInt
    {
        BadLogLevel = 1,
        InvalidBufferRange = 3,
    };

    enum class ProgramEventType : uint32_t
    {
        Null = 0,
        ExitGracefully = 1,
        ExitImmediately = 2,
        IncomingMail = 3,
    };

    //TODO: this is not necessary, lets move to errors 1-32 being generic errors usable by all
    enum class ProgramEventError : NativeUInt
    {
        InvalidBufferRange = 3,
    };
}
