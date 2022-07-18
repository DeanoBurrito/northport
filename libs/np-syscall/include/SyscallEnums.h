#pragma once

#include <NativePtr.h>

namespace np::Syscall
{
    enum class SyscallGroupId : NativeUInt
    {
        Experiments = 0x0,
        Memory = 0x1,
        Devices = 0x2,
        Filesystem = 0x3,
        Ipc = 0x4,
        Utilities = 0x5,
        ProgramEvents = 0x6,
        LocalThreadControl = 0x7,
    };

    constexpr static size_t SyscallGroupVersions[] = 
    {
        1, //Experiments
        1, //Memory
        1, //Devices
        1, //Filesystem
        1, //Ipc
        1, //Utilities
        1, //ProgramEvents
        1, //LocalThreadControl
    };
    
#define MAKE_SCID(group, offset) ((NativeUInt)SyscallGroupId::group << 32) + offset
    enum class SyscallId : NativeUInt
    {
        LoopbackTest = MAKE_SCID(Experiments, 0),

        MapMemory = MAKE_SCID(Memory, 0),
        UnmapMemory = MAKE_SCID(Memory, 1),
        ModifyMemoryFlags = MAKE_SCID(Memory, 2),

        GetPrimaryDeviceInfo = MAKE_SCID(Devices, 0),
        GetDevicesOfType = MAKE_SCID(Devices, 1),
        GetDeviceInfo = MAKE_SCID(Devices, 2),
        DeviceEventControl = MAKE_SCID(Devices, 3),
        GetAggregateId = MAKE_SCID(Devices, 4),

        GetFileInfo = MAKE_SCID(Filesystem, 0),
        OpenFile = MAKE_SCID(Filesystem, 1),
        CloseFile = MAKE_SCID(Filesystem, 2),
        ReadFromFile = MAKE_SCID(Filesystem, 3),
        WriteToFile = MAKE_SCID(Filesystem, 4),
        FlushFile = MAKE_SCID(Filesystem, 5),

        StartIpcStream = MAKE_SCID(Ipc, 0),
        StopIpcStream = MAKE_SCID(Ipc, 1),
        OpenIpcStream = MAKE_SCID(Ipc, 2),
        CloseIpcStream = MAKE_SCID(Ipc, 3),
        ReadFromIpcStream = MAKE_SCID(Ipc, 4),
        WriteToIpcStream = MAKE_SCID(Ipc, 5),
        CreateMailbox = MAKE_SCID(Ipc, 6),
        DestroyMailbox = MAKE_SCID(Ipc, 7),
        PostToMailbox = MAKE_SCID(Ipc, 8),
        ModifyIpcConfig = MAKE_SCID(Ipc, 9),

        Log = MAKE_SCID(Utilities, 0),
        GetVersion = MAKE_SCID(Utilities, 1),

        PeekNextEvent = MAKE_SCID(ProgramEvents, 0),
        ConsumeNextEvent = MAKE_SCID(ProgramEvents, 1),
        GetPendingEventCount = MAKE_SCID(ProgramEvents, 2),

        Sleep = MAKE_SCID(LocalThreadControl, 0),
        Exit = MAKE_SCID(LocalThreadControl, 1),
        GetId = MAKE_SCID(LocalThreadControl, 2),
    };
#undef MAKE_SCID

    enum class GeneralError : NativeUInt
    {
        Success = 0,
        InvalidSyscallId = 1,
        InvalidBufferRange = 2,
        BadHandleId = 3,

        MaxGeneralError = 0xFF,
    };

    enum class MemoryMapFlags : NativeUInt
    {
        None = 0,
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
        FeatureNotAvailable = 0x100,
        NoPrimaryDevice = 0x101,
        UnknownDeviceType = 0x102,
        MismatchedDeviceType = 0x103,
    };

    enum class FileError : NativeUInt
    {
        FileNotFound = 0x100,
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
        StreamStartFail = 0x100,
        MailDeliveryFailed = 0x101,
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
        Debug = 5,
    };

    enum class LogError : NativeUInt
    {
        BadLogLevel = 0x100,
    };

    enum class ProgramEventType : uint32_t
    {
        Null = 0,
        ExitGracefully = 1,
        ExitImmediately = 2,
        IncomingMail = 3,
        KeyboardEvent = 4,
        MouseEvent = 5,
    };

    enum class GetIdType : NativeUInt
    {
        Thread = 0,
        ThreadGroup = 1,
    };
}
