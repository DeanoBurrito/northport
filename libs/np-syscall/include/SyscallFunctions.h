#pragma once

#include <NativePtr.h>
#include <Optional.h>
#include <containers/Vector.h>
#include <SyscallStructs.h>
#include <SyscallEnums.h>
#include <String.h>

namespace np::Syscall
{
    struct SyscallData
    { 
        uint64_t id, arg0, arg1, arg2, arg3; 

        SyscallData() = default;
        constexpr SyscallData(uint64_t id, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3)
        : id(id), arg0(arg0), arg1(arg1), arg2(arg2), arg3(arg3)
        {}
    };

    [[gnu::always_inline]]
    inline void DoSyscall(SyscallData* data)
    {
        asm volatile("\
            mov 0x00(%0), %%rax; \
            mov 0x08(%0), %%rdi; \
            mov 0x10(%0), %%rsi; \
            mov 0x18(%0), %%rdx; \
            mov 0x20(%0), %%rcx; \
            int $0x24; \
            mov %%rax, 0x00(%0); \
            mov %%rdi, 0x08(%0); \
            mov %%rsi, 0x10(%0); \
            mov %%rdx, 0x18(%0); \
            mov %%rcx, 0x20(%0); \
            " 
            : "=r"(data)
            : "r"(data)
            : "rax", "rdi", "rsi", "rdx", "rcx", "memory"
            );
    }

    //0x0* - testing 
    bool LoopbackTest();

    //0x1* - memory
    MappedMemoryDetails MapMemory(NativeUInt base, size_t bytesLength, MemoryMapFlags flags);
    NativeUInt UnmapMemory(NativeUInt base, size_t bytesLength);
    MappedMemoryDetails ModifyMemoryFlags(NativeUInt base, size_t bytesLength, MemoryMapFlags flags);

    //0x2* - devices
    sl::Opt<BasicDeviceInfo> GetPrimaryDeviceInfo(DeviceType type);
    sl::Opt<sl::Vector<BasicDeviceInfo>> GetDevicesOfType(DeviceType type);
    sl::Opt<DetailedDeviceInfo*> GetDeviceInfo(size_t deviceId);

    //0x3* - filesystem
    sl::Opt<FileInfo*> GetFileInfo(const sl::String& filepath); 
    [[nodiscard]]
    sl::Opt<FileHandle> OpenFile(const sl::String& filepath);
    void CloseFile(FileHandle handle);
    size_t ReadFromFile(FileHandle file, uint32_t readFromOffset, uint32_t outputOffset, const uint8_t* outputBuffer, size_t readLength);
    size_t WriteToFile(FileHandle handle, uint32_t writeToOffset, uint32_t inputOffset, const uint8_t* inputBuffer, size_t writeLength);

    //TODO: we are returning pointers allocated by the kernel in a few of these. How do we free them? Who is responsible for that memory now?
}
