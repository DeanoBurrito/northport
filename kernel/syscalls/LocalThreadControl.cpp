#include <syscalls/Dispatch.h>
#include <scheduling/Thread.h>

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
        //TODO: we should do something with the exit code - maybe later?
        Scheduling::Thread::Current()->Exit();
        __builtin_unreachable();
    }
}
