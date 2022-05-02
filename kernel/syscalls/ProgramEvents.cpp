#include <syscalls/Dispatch.h>
#include <scheduling/Scheduler.h>

namespace Kernel::Syscalls
{
    void PeekNextEvent(SyscallRegisters& regs)
    {
        auto maybeEvent = Scheduling::ThreadGroup::Current()->PeekEvent();
        if (!maybeEvent)
        {
            regs.arg0 = 0;
            return;
        }

        regs.arg0 = (uint32_t)maybeEvent->type | ((uint64_t)maybeEvent->length << 32);
    }

    void ConsumeNextEvent(SyscallRegisters& regs)
    {}

    void GetPendingEventCount(SyscallRegisters& regs)
    {
        regs.arg0 = Scheduling::ThreadGroup::Current()->PendingEventCount();
    }
}
