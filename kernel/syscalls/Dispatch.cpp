#include <syscalls/Dispatch.h>
#include <SyscallEnums.h>
#include <memory/PhysicalMemory.h>

extern "C"
{
    [[noreturn]]
    extern void DoInterruptReturn(Kernel::StoredRegisters* regs); //defined in IdtStub.s
}

namespace Kernel::Syscalls
{
    StoredRegisters* EnterSyscall(StoredRegisters* regs)
    {
        StoredRegisters* tempRegs = new StoredRegisters();
        tempRegs->iret_flags = 0x202;
        tempRegs->iret_cs = GDT_ENTRY_RING_0_CODE;
        tempRegs->iret_rip = (NativeUInt)Dispatch;
        tempRegs->iret_ss = GDT_ENTRY_RING_0_DATA;
        tempRegs->iret_rsp = sl::NativePtr(regs).raw - sizeof(StoredRegisters); //re-use existing stack, but leave a buffer zone between us and the existing regds.

        tempRegs->errorCode = 0;
        tempRegs->vectorNumber = 0;

        tempRegs->rax = tempRegs->rbx = tempRegs->rcx = 0;
        tempRegs->rsp = tempRegs->rbp = tempRegs->iret_rsp;
        tempRegs->r8 = tempRegs->r9 = tempRegs->r10 = tempRegs->r11 = 0;
        tempRegs->r12 = tempRegs->r13 = tempRegs->r14 = tempRegs->r15 = 0;

        //we're using the sys v x86_64 calling convention here
        tempRegs->rdi = (NativeUInt)regs;
        tempRegs->rsi = (NativeUInt)tempRegs;

        return tempRegs;
    }
    
    [[noreturn]]
    void Dispatch(StoredRegisters* regs, StoredRegisters* deleteMePlease)
    {
        delete deleteMePlease;
        deleteMePlease = nullptr;
        
        using namespace np::Syscall;
        SyscallId attemptedId = static_cast<SyscallId>(regs->rax);

        SyscallRegisters syscallRegs(regs);
        syscallRegs.id = SyscallSuccess; //assume success by default, unless overriden by not found or error code.
        
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

            default:
                syscallRegs.id = SyscallNotFound;
                break;
        }

        //move data back to orginal regs, and if we didnt succeed wipe the data registers.
        syscallRegs.Transpose(regs);
        if (regs->rax != SyscallSuccess)
            regs->rdi = regs->rsi = regs->rdx = regs->rcx = 0;

        CPU::ClearInterruptsFlag(); //disable interrupts while we do some critical stuff
        DoInterruptReturn(regs);
        __builtin_unreachable();
    }
}
