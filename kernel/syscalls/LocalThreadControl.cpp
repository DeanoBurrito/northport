#include <syscalls/Dispatch.h>
#include <scheduling/Thread.h>
#include <SyscallEnums.h>
#include <Log.h>

namespace Kernel::Syscalls
{
    void Sleep(SyscallRegisters& regs)
    {
        const bool wakeOnEvents = (regs.arg1 != 0);
        if (wakeOnEvents)
        {
            if (Scheduling::ThreadGroup::Current()->PendingEventCount() != 0)
                return;
            
            Scheduling::Thread::Current()->SleepUntilEvent(regs.arg0);
        }
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

    void GetId(SyscallRegisters& regs)
    {
        using np::Syscall::GetIdType;
        GetIdType type = (GetIdType)regs.arg0;

        switch (type)
        {
            case GetIdType::Thread:
                regs.arg0 = Scheduling::Thread::Current()->Id();
                break;

            case GetIdType::ThreadGroup:
                regs.arg0 = Scheduling::ThreadGroup::Current()->Id();
                break;
            
            default:
                regs.id = 1;
                break;
        }
    }
}
