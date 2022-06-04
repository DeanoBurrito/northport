#include <syscalls/Dispatch.h>
#include <memory/VirtualMemory.h>
#include <SyscallEnums.h>
#include <scheduling/Thread.h>
#include <Log.h>

namespace Kernel::Syscalls
{
    void Log(SyscallRegisters& regs)
    {
        if (!Memory::VMM::Current()->RangeExists({ regs.arg0, PAGE_FRAME_SIZE }))
        {
            regs.id = (uint64_t)np::Syscall::LogError::InvalidBufferRange;
            return;
        }

        if (regs.arg1 >= (uint64_t)LogSeverity::EnumCount)
        {
            regs.id = (uint64_t)np::Syscall::LogError::BadLogLevel;
            return;
        }

        if (regs.arg1 == (uint64_t)LogSeverity::Fatal)
            regs.arg1 = (uint64_t)LogSeverity::Error;
        
        const Scheduling::Thread* currentThread = Scheduling::Thread::Current();
        CPU::AllowSumac(true);
        //passing a user-controlled pointer might look dangerous, but its not processed as part of the formatting, its only copied to the output.
        Kernel::Logf("[p:%u, t:%u] %s", (LogSeverity)regs.arg1, currentThread->Parent()->Id(), currentThread->Id(), reinterpret_cast<const char*>(regs.arg0));
        CPU::AllowSumac(false);
    }
}
