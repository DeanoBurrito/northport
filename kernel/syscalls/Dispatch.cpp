#include <syscalls/Dispatch.h>
#include <SyscallEnums.h>
#include <memory/PhysicalMemory.h>
#include <Configuration.h>
#include <Log.h>

extern "C"
{
    [[noreturn]]
    extern void TrapExit(Kernel::StoredRegisters* regs); //defined in arch/x86_64/Trap.s
}

namespace Kernel::Syscalls
{
    StoredRegisters* EnterSyscall(StoredRegisters* regs)
    {
        //We're going to be a bit cheeky here: we need a temporary iret frame so we can return somewhere
        //else, and preserve the current one (back to the running program).
        //Assuming the stack is *at least* 1 page (its usually 4-8), we can just pick a space in the middle
        //somewhere. 
        //The big danger here is the compiler, since its naturally eating into the stack on it's own.
        //I figure 0x500 bytes should be fine for now, if there's weird crashes, maybe we increase it.
        sl::NativePtr regsStack = sl::NativePtr(regs).raw - 0x500;

        StoredRegisters* tempRegs = regsStack.As<StoredRegisters>();
        tempRegs->iret_flags = 0x202;
        tempRegs->iret_cs = GDT_ENTRY_RING_0_CODE;
        tempRegs->iret_rip = (NativeUInt)Dispatch;
        tempRegs->iret_ss = GDT_ENTRY_RING_0_DATA;
        tempRegs->iret_rsp = (NativeUInt)regs;

        tempRegs->errorCode = 0;
        tempRegs->vectorNumber = 0;

        tempRegs->rax = tempRegs->rbx = tempRegs->rcx = 0;
        tempRegs->rsp = tempRegs->rbp = tempRegs->iret_rsp;
        tempRegs->r8 = tempRegs->r9 = tempRegs->r10 = tempRegs->r11 = 0;
        tempRegs->r12 = tempRegs->r13 = tempRegs->r14 = tempRegs->r15 = 0;

        //we're using the sys v x86_64 calling convention here
        tempRegs->rdi = (NativeUInt)regs;

        return tempRegs;
    }
    
    [[noreturn]]
    void Dispatch(StoredRegisters* regs)
    {
        using namespace np::Syscall;
        SyscallId attemptedId = static_cast<SyscallId>(regs->rax);

        SyscallRegisters syscallRegs(regs);
        syscallRegs.id = SyscallSuccess; //assume success by default, unless overriden by not found or error code.

        auto enableRequestLog = Configuration::Global()->Get("syscall_log_requests");
        if (enableRequestLog && enableRequestLog->integer == true)
            Logf("Syscall request: id=0x%lx, arg0=0x%lx, arg1=0x%lx, arg2=0x%lx, arg3=0x%lx", LogSeverity::Debug,
            (uint64_t)attemptedId, syscallRegs.arg0, syscallRegs.arg1, syscallRegs.arg2, syscallRegs.arg3);
        
        switch (attemptedId)
        {
            case SyscallId::LoopbackTest:
                syscallRegs.id = SyscallSuccess;
                syscallRegs.arg0 = syscallRegs.arg1 = syscallRegs.arg2 = syscallRegs.arg3 = 0;
                break;

            case SyscallId::MapMemory:
                MapMemory(syscallRegs);
                syscallRegs.arg2 = syscallRegs.arg3 = 0;
                break;
            case SyscallId::UnmapMemory:
                UnmapMemory(syscallRegs);
                syscallRegs.arg1 = syscallRegs.arg2 = syscallRegs.arg3 = 0;
                break;
            case SyscallId::ModifyMemoryFlags:
                ModifyMemoryFlags(syscallRegs);
                syscallRegs.arg2 = syscallRegs.arg3 = 0;
                break;
            
            case SyscallId::GetPrimaryDeviceInfo:
                GetPrimaryDeviceInfo(syscallRegs);
                break;
            case SyscallId::GetDevicesOfType:
                GetDevicesOfType(syscallRegs);
                syscallRegs.arg0 = syscallRegs.arg1 = 0;
                break;
            case SyscallId::GetDeviceInfo:
                GetDeviceInfo(syscallRegs);
                break;
            case SyscallId::EnableDeviceEvents:
                EnableDeviceEvents(syscallRegs);
                syscallRegs.arg0 = syscallRegs.arg1 = syscallRegs.arg2 = syscallRegs.arg3 = 0;
                break;
            case SyscallId::DisableDeviceEvents:
                DisableDeviceEvents(syscallRegs);
                syscallRegs.arg0 = syscallRegs.arg1 = syscallRegs.arg2 = syscallRegs.arg3 = 0;
                break;
            case SyscallId::GetAggregateId:
                GetAggregateId(syscallRegs);
                syscallRegs.arg1 = syscallRegs.arg2 = syscallRegs.arg3 = 0;
                break;

            case SyscallId::GetFileInfo:
                GetFileInfo(syscallRegs);
                syscallRegs.arg0 = syscallRegs.arg1 = syscallRegs.arg2 = 0;
                break;
            case SyscallId::OpenFile:
                OpenFile(syscallRegs);
                syscallRegs.arg1 = syscallRegs.arg2 = syscallRegs.arg3 = 0;
                break;
            case SyscallId::CloseFile:
                CloseFile(syscallRegs);
                syscallRegs.arg0 = syscallRegs.arg1 = syscallRegs.arg2 = syscallRegs.arg3 = 0;
                break;
            case SyscallId::ReadFromFile:
                ReadFromFile(syscallRegs);
                syscallRegs.arg1 = syscallRegs.arg2 = syscallRegs.arg3 = 0;
                break;
            case SyscallId::WriteToFile:
                WriteToFile(syscallRegs);
                syscallRegs.arg1 = syscallRegs.arg2 = syscallRegs.arg3 = 0;
                break;

            case SyscallId::StartIpcStream:
                StartIpcStream(syscallRegs);
                syscallRegs.arg3 = 0;
                break;
            case SyscallId::StopIpcStream:
                StopIpcStream(syscallRegs);
                syscallRegs.arg0 = syscallRegs.arg1 = syscallRegs.arg2 = syscallRegs.arg3 = 0;
                break;
            case SyscallId::OpenIpcStream:
                OpenIpcStream(syscallRegs);
                syscallRegs.arg3 = 0;
                break;
            case SyscallId::CloseIpcStream:
                CloseIpcStream(syscallRegs);
                syscallRegs.arg0 = syscallRegs.arg1 = syscallRegs.arg2 = syscallRegs.arg3 = 0;
                break;
            case SyscallId::CreateMailbox:
                CreateMailbox(syscallRegs);
                syscallRegs.arg1 = syscallRegs.arg2 = syscallRegs.arg3 = 0;
                break;
            case SyscallId::DestroyMailbox:
                DestroyMailbox(syscallRegs);
                syscallRegs.arg0 = syscallRegs.arg1 = syscallRegs.arg2 = syscallRegs.arg3 = 0;
                break;
            case SyscallId::PostToMailbox:
                PostToMailbox(syscallRegs);
                syscallRegs.arg0 = syscallRegs.arg1 = syscallRegs.arg2 = syscallRegs.arg3 = 0;
                break;
            case SyscallId::ModifyIpcConfig:
                ModifyIpcConfig(syscallRegs);
                syscallRegs.arg0 = syscallRegs.arg1 = syscallRegs.arg2 = syscallRegs.arg3 = 0;
                break;
            
            case SyscallId::Log:
                Log(syscallRegs);
                syscallRegs.arg0 = syscallRegs.arg1 = syscallRegs.arg2 = syscallRegs.arg3 = 0;
                break;

            case SyscallId::PeekNextEvent:
                PeekNextEvent(syscallRegs);
                syscallRegs.arg3 = 0;
                break;
            case SyscallId::ConsumeNextEvent:
                ConsumeNextEvent(syscallRegs);
                syscallRegs.arg3 = 0;
                break;
            case SyscallId::GetPendingEventCount:
                GetPendingEventCount(syscallRegs);
                syscallRegs.arg1 = syscallRegs.arg2 = syscallRegs.arg3 = 0;
                break;

            case SyscallId::Sleep:
                Sleep(syscallRegs);
                syscallRegs.arg0 = syscallRegs.arg1 = syscallRegs.arg2 = syscallRegs.arg3 = 0;
                break;
            case SyscallId::Exit:
                Exit(syscallRegs);
                __builtin_unreachable();
            case SyscallId::GetId:
                GetId(syscallRegs);
                syscallRegs.arg1 = syscallRegs.arg2 = syscallRegs.arg3 = 0;
                break;

            default:
                syscallRegs.id = SyscallNotFound;
                break;
        }

        //move data back to orginal regs, and if we didnt succeed wipe the data registers.
        syscallRegs.Transpose(regs);
        if (regs->rax != SyscallSuccess)
            regs->rdi = regs->rsi = regs->rdx = regs->rcx = 0;
        
        auto enableResponseLog = Configuration::Global()->Get("syscall_log_responses");
        if (enableResponseLog && enableResponseLog->integer == true)
            Logf("Syscall response: id=0x%lx, arg0=0x%lx, arg1=0x%lx, arg2=0x%lx, arg3=0x%lx", LogSeverity::Debug,
            (uint64_t)attemptedId, syscallRegs.arg0, syscallRegs.arg1, syscallRegs.arg2, syscallRegs.arg3);

        CPU::DisableInterrupts(); //disable interrupts while we do some critical stuff
        TrapExit(regs);
        __builtin_unreachable();
    }
}
