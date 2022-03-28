#include <SyscallEnums.h>
#include <SyscallFunctions.h>

namespace np::Syscall
{
    bool LoopbackTest()
    {
        SyscallData data((uint64_t)SyscallId::LoopbackTest, 0, 0, 0, 0);
        DoSyscall(&data);

        return data.id == SyscallSuccess;
    }

    MappedMemoryDetails MapMemory(NativeUInt base, size_t bytesLength, MemoryMapFlags flags)
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

    MappedMemoryDetails ModifyMemoryFlags(NativeUInt base, size_t bytesLength, MemoryMapFlags flags)
    {
        SyscallData data((uint64_t)SyscallId::ModifyMemoryFlags, base, bytesLength, (uint64_t)flags, 0);
        DoSyscall(&data);

        return { data.arg0, data.arg1 };
    }

    sl::Opt<BasicDeviceInfo> GetPrimaryDeviceInfo(DeviceType type)
    {
        SyscallData data((uint64_t)SyscallId::GetPrimaryDeviceInfo, (uint64_t)type, 0, 0, 0);
        DoSyscall(&data);

        if (data.id != SyscallSuccess)
            return {};
        return BasicDeviceInfo(data.arg0, data.arg1, data.arg2, data.arg3);
    }

    sl::Opt<sl::Vector<BasicDeviceInfo>> GetDevicesOfType(DeviceType type)
    {
        SyscallData data((uint64_t)SyscallId::GetPrimaryDeviceInfo, (uint64_t)type, 0, 0, 0);
        DoSyscall(&data);

        if (data.id != SyscallSuccess)
            return {};
        return sl::Vector<BasicDeviceInfo>(sl::NativePtr(data.arg3).As<BasicDeviceInfo>(), data.arg2);
    }

    sl::Opt<DetailedDeviceInfo*> GetDeviceInfo(size_t deviceId)
    {
        SyscallData data((uint64_t)SyscallId::GetDeviceInfo, deviceId, 0, 0, 0);
        DoSyscall(&data);

        if (data.id != SyscallSuccess)
            return {};
        return sl::NativePtr(data.arg3).As<DetailedDeviceInfo>();
    }

    sl::Opt<FileInfo*> GetFileInfo(const sl::String& filepath)
    {
        SyscallData data((uint64_t)SyscallId::GetFileInfo, (uint64_t)filepath.C_Str(), 0, 0, 0);
        DoSyscall(&data);

        if (data.id != SyscallSuccess)
            return {};
        return sl::NativePtr(data.arg3).As<FileInfo>();
    }

    sl::Opt<FileHandle> OpenFile(const sl::String& filepath)
    {
        SyscallData data((uint64_t)SyscallId::OpenFile, (uint64_t)filepath.C_Str(), 0, 0, 0);
        DoSyscall(&data);

        if (data.id != SyscallSuccess)
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

        if (data.id != SyscallSuccess)
            return {};
        return data.arg0;
    }

    size_t WriteToFile(FileHandle handle, uint32_t writeToOffset, uint32_t inputOffset, const uint8_t* inputBuffer, size_t writeLength)
    {

    }
}
