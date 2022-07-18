#include <SyscallEnums.h>
#include <SyscallFunctions.h>

namespace np::Syscall
{
    constexpr uint64_t SuccessVal = (uint64_t)GeneralError::Success;
    
    bool LoopbackTest()
    {
        SyscallData data((uint64_t)SyscallId::LoopbackTest, 0, 0, 0, 0);
        DoSyscall(&data);

        return data.id == SuccessVal;
    }

    sl::BufferView MapMemory(NativeUInt base, size_t bytesLength, MemoryMapFlags flags)
    {
        SyscallData data((uint64_t)SyscallId::MapMemory, base, bytesLength, (uint64_t)flags, 0);
        DoSyscall(&data);

        return { data.arg0, data.arg1 };
    }

    NativeUInt UnmapMemory(NativeUInt base, size_t bytesLength)
    {
        SyscallData data((uint64_t)SyscallId::UnmapMemory, base, bytesLength, 0, 0);
        DoSyscall(&data);

        return data.arg0;
    }

    sl::BufferView ModifyMemoryFlags(NativeUInt base, size_t bytesLength, MemoryMapFlags flags)
    {
        SyscallData data((uint64_t)SyscallId::ModifyMemoryFlags, base, bytesLength, (uint64_t)flags, 0);
        DoSyscall(&data);

        return { data.arg0, data.arg1 };
    }

    sl::Opt<DeviceInfo> GetPrimaryDeviceInfo(DeviceType type)
    {
        SyscallData data((uint64_t)SyscallId::GetPrimaryDeviceInfo, (uint64_t)type, 0, 0, 0);
        DoSyscall(&data);

        if (data.id != SuccessVal)
            return {};
        return DeviceInfo(data.arg0, data.arg1, data.arg2, data.arg3);
    }

    sl::Opt<sl::Vector<DeviceInfo>> GetDevicesOfType(DeviceType type)
    {
        SyscallData data((uint64_t)SyscallId::GetPrimaryDeviceInfo, (uint64_t)type, 0, 0, 0);
        DoSyscall(&data);

        if (data.id != SuccessVal)
            return {};
        return sl::Vector<DeviceInfo>(sl::NativePtr(data.arg3).As<DeviceInfo>(), data.arg2);
    }

    sl::Opt<DeviceInfo> GetDeviceInfo(size_t deviceId)
    {
        SyscallData data((uint64_t)SyscallId::GetDeviceInfo, deviceId, 0, 0, 0);
        DoSyscall(&data);

        if (data.id != SuccessVal)
            return {};
        return DeviceInfo(data.arg0, data.arg1, data.arg2, data.arg3);
    }

    void DeviceEventControl(size_t deviceId, bool subscribe)
    {
        SyscallData data((uint64_t)SyscallId::DeviceEventControl, deviceId, subscribe ? 1 : 0, 0, 0);
        DoSyscall(&data);
    }

    sl::Opt<size_t> GetAggregateId(DeviceType type)
    {
        SyscallData data((uint64_t)SyscallId::GetAggregateId, (uint64_t)type, 0, 0, 0);
        DoSyscall(&data);

        if (data.id != SuccessVal)
            return {};
        return data.arg0;
    }

    sl::Opt<FileInfo> GetFileInfo(const sl::String& filepath)
    {
        SyscallData data((uint64_t)SyscallId::GetFileInfo, (uint64_t)filepath.C_Str(), 0, 0, 0);
        DoSyscall(&data);

        if (data.id != SuccessVal)
            return {};
        
        FileInfo info;
        info.fileSize = data.arg0;
        return info;
    }

    sl::Opt<FileHandle> OpenFile(const sl::String& filepath)
    {
        SyscallData data((uint64_t)SyscallId::OpenFile, (uint64_t)filepath.C_Str(), 0, 0, 0);
        DoSyscall(&data);

        if (data.id != SuccessVal)
            return {};
        return data.arg0;
    }

    void CloseFile(FileHandle handle)
    {
        SyscallData data((uint64_t)SyscallId::CloseFile, handle, 0, 0, 0);
        DoSyscall(&data);
    }

    size_t ReadFromFile(FileHandle file, uint32_t readFromOffset, uint32_t outputOffset, const uint8_t* outputBuffer, size_t readLength)
    {
        const uint64_t arg1 = readFromOffset | (uint64_t)outputOffset << 32;
        SyscallData data((uint64_t)SyscallId::ReadFromFile, file, arg1, (uint64_t)outputBuffer, readLength);
        DoSyscall(&data);

        if (data.id != SuccessVal)
            return 0;
        return data.arg0;
    }

    size_t WriteToFile(FileHandle handle, uint32_t writeToOffset, uint32_t inputOffset, const uint8_t* inputBuffer, size_t writeLength)
    {
        const uint64_t arg1 = writeToOffset | (uint64_t)inputOffset << 32;
        SyscallData data((uint64_t)SyscallId::WriteToFile, handle, arg1, (uint64_t)inputBuffer, writeLength);
        DoSyscall(&data);

        if (data.id != SuccessVal)
            return 0;
        return data.arg0;
    }

    sl::Opt<IpcHandle> StartIpcStream(const sl::String& name, IpcStreamFlags flags, size_t& streamSize, NativeUInt& bufferAddr)
    {
        SyscallData data((uint64_t)SyscallId::StartIpcStream, (uint64_t)name.C_Str(), (uint64_t)flags, streamSize, 0);
        DoSyscall(&data);

        if (data.id != SuccessVal)
            return {};
        
        streamSize = data.arg1;
        bufferAddr = data.arg2;
        return data.arg0;
    }

    void StopIpcStream(IpcHandle handle)
    {
        SyscallData data((uint64_t)SyscallId::StopIpcStream, handle, 0, 0, 0);
        DoSyscall(&data);
    }

    sl::Opt<IpcHandle> OpenIpcStream(const sl::String& name, IpcStreamFlags flags, NativeUInt& bufferAddr)
    {
        SyscallData data((uint64_t)SyscallId::OpenIpcStream, (uint64_t)name.C_Str(), (uint64_t)flags, 0, 0);
        DoSyscall(&data);

        if (data.id != SuccessVal)
            return {};
        
        bufferAddr = data.arg2;
        return data.arg0;
    }

    void CloseIpcStream(IpcHandle handle)
    {
        SyscallData data((uint64_t)SyscallId::CloseIpcStream, handle, 0, 0, 0);
        DoSyscall(&data);
    }

    sl::Opt<IpcHandle> CreateMailbox(const sl::String& name, IpcMailboxFlags flags)
    {
        SyscallData data((uint64_t)SyscallId::CreateMailbox, (uint64_t)name.C_Str(), (uint64_t)flags, 0, 0);
        DoSyscall(&data);

        if (data.id != SuccessVal)
            return {};

        return data.arg0;
    }

    void DestroyMailbox(IpcHandle handle)
    {
        SyscallData data((uint64_t)SyscallId::DestroyMailbox, handle, 0, 0, 0);
        DoSyscall(&data);
    }

    bool PostToMailbox(const sl::String& name, sl::BufferView mail, bool leaveOpenHint)
    {
        SyscallData data((uint64_t)SyscallId::PostToMailbox, (uint64_t)name.C_Str(), mail.base.raw, mail.length, leaveOpenHint ? 1 : 0);
        DoSyscall(&data);

        return data.id == SuccessVal;
    }

    void ModifyIpcConfig(IpcConfigOperation op, NativeUInt arg1, NativeUInt arg2, NativeUInt arg3)
    {
        SyscallData data((uint64_t)SyscallId::ModifyIpcConfig, (uint64_t)op, arg1, arg2, arg3);
        DoSyscall(&data);
    }

    void Log(const sl::String& text, LogLevel level)
    {
        SyscallData data((uint64_t)SyscallId::Log, (uint64_t)text.C_Str(), (uint64_t)level, 0, 0);
        DoSyscall(&data);
    }

    sl::Opt<size_t> GetVersion(SyscallGroupId group)
    {
        SyscallData data((uint64_t)SyscallId::GetVersion, (uint64_t)group, 0, 0, 0);
        DoSyscall(&data);

        if (data.id != SuccessVal)
            return {};
        return data.arg0;
    }
    
    sl::Opt<ProgramEvent> PeekNextEvent()
    {
        SyscallData data((uint64_t)SyscallId::PeekNextEvent, 0, 0, 0, 0);
        DoSyscall(&data);

        if (data.id != SuccessVal)
            return {};
        
        ProgramEvent event;
        event.type = static_cast<ProgramEventType>(data.arg0 & 0xFFFF'FFFF);
        event.dataLength = data.arg0 >> 32;
        event.handle = data.arg1;
        event.sender = data.arg2;
        return event;
    }

    sl::Opt<ProgramEvent> ConsumeNextEvent(sl::BufferView buffer)
    {
        SyscallData data((uint64_t)SyscallId::ConsumeNextEvent, buffer.base.raw, buffer.length, 0, 0);
        DoSyscall(&data);

        if (data.id != SuccessVal)
            return {};
        
        ProgramEvent event;
        event.type = static_cast<ProgramEventType>(data.arg0 & 0xFFFF'FFFF);
        event.dataLength = data.arg0 >> 32;
        event.handle = data.arg1;
        event.sender = data.arg2;
        return event;
    }

    size_t GetPendingEventCount()
    {
        SyscallData data((uint64_t)SyscallId::GetPendingEventCount, 0, 0, 0, 0);
        DoSyscall(&data);

        if (data.id == SuccessVal)
            return data.arg0;
        return 0;
    }

    void Sleep(size_t timeout, bool wakeOnEvents)
    {
        SyscallData data((uint64_t)SyscallId::Sleep, timeout, wakeOnEvents ? 1 : 0, 0, 0);
        DoSyscall(&data);
    }

    [[noreturn]]
    void Exit(unsigned long exitCode)
    {
        SyscallData data((uint64_t)SyscallId::Exit, exitCode, 0, 0, 0);
        DoSyscall(&data);
        __builtin_unreachable();
    }

    size_t GetId(GetIdType idType)
    {
        SyscallData data((uint64_t)SyscallId::GetId, (uint64_t)idType, 0, 0, 0);
        DoSyscall(&data);
        return data.arg0;
    }
}
