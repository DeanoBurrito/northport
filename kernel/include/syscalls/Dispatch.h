#pragma once

#include <Platform.h>

namespace Kernel::Syscalls
{
    struct SyscallRegisters
    {
        uint64_t id;
        uint64_t arg0;
        uint64_t arg1;
        uint64_t arg2;
        uint64_t arg3;

        SyscallRegisters() : id(0), arg0(0), arg1(0), arg2(0), arg3(0)
        {}

        SyscallRegisters(uint64_t id, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3) 
        : id(id), arg0(arg0), arg1(arg1), arg2(arg2), arg3(arg3)
        {}

        SyscallRegisters(StoredRegisters* nativeRegs)
        : id(nativeRegs->rax), arg0(nativeRegs->rdi), arg1(nativeRegs->rsi), arg2(nativeRegs->rdx), arg3(nativeRegs->rcx)
        {}

        [[gnu::always_inline]]
        inline void Transpose(StoredRegisters* nativeRegs)
        {
            nativeRegs->rax = id;
            nativeRegs->rdi = arg0;
            nativeRegs->rsi = arg1;
            nativeRegs->rdx = arg2;
            nativeRegs->rcx = arg3;
        }
    };
    
    StoredRegisters* EnterSyscall(StoredRegisters* regs);
    [[noreturn]]
    void Dispatch(StoredRegisters* regs, StoredRegisters* deleteMePlease);

    //0x0* - testing
    void LoopbackTest(SyscallRegisters& regs);

    //0x1* - memory
    void MapMemory(SyscallRegisters& regs);
    void UnmapMemory(SyscallRegisters& regs);
    void ModifyMemoryFlags(SyscallRegisters& regs);
    
    //0x2* - devices
    void GetPrimaryDeviceInfo(SyscallRegisters& regs);
    void GetDevicesOfType(SyscallRegisters& regs);
    void GetDeviceInfo(SyscallRegisters& regs);
    void EnableDeviceEvents(SyscallRegisters& regs);
    void DisableDeviceEvents(SyscallRegisters& regs);

    //0x3* - filesystem
    void GetFileInfo(SyscallRegisters& regs);
    void OpenFile(SyscallRegisters& regs);
    void CloseFile(SyscallRegisters& regs);
    void ReadFromFile(SyscallRegisters& regs);
    void WriteToFile(SyscallRegisters& regs);
    
    //0x4* - ipc
    void StartIpcStream(SyscallRegisters& regs);
    void StopIpcStream(SyscallRegisters& regs);
    void OpenIpcStream(SyscallRegisters& regs);
    void CloseIpcStream(SyscallRegisters& regs);
    void CreateMailbox(SyscallRegisters& regs);
    void DestroyMailbox(SyscallRegisters& regs);
    void PostToMailbox(SyscallRegisters& regs);
    void ModifyIpcConfig(SyscallRegisters& regs);

    //0x5* - utilities
    void Log(SyscallRegisters& regs);

    //0x6* - program events
    void PeekNextEvent(SyscallRegisters& regs);
    void ConsumeNextEvent(SyscallRegisters& regs);
    void GetPendingEventCount(SyscallRegisters& regs);
}
