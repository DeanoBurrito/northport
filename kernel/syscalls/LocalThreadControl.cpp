#include <syscalls/Dispatch.h>
#include <scheduling/Thread.h>
#include <Log.h>

namespace Kernel::Syscalls
{
    void Sleep(SyscallRegisters& regs)
    {
        const bool wakeOnEvents = (regs.arg1 != 0);
        if (wakeOnEvents)
            Scheduling::Thread::Current()->SleepUntilEvent(regs.arg0);
        else
            Scheduling::Thread::Current()->Sleep(regs.arg0);
    }

    [[noreturn]]
    void Exit(SyscallRegisters& regs)
    {
        const Scheduling::Thread* currentThread = Scheduling::Thread::Current();
        Logf("Thread %u exited with code 0x%lx", LogSeverity::Info, currentThread->Id(), regs.arg0);

        Scheduling::Thread::Current()->Exit();
        __builtin_unreachable();
    }
}
