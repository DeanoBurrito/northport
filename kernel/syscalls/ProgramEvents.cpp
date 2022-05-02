#include <syscalls/Dispatch.h>
#include <scheduling/Scheduler.h>
#include <memory/IpcMailbox.h>
#include <memory/IpcManager.h>
#include <SyscallEnums.h>
#include <Locks.h>

namespace Kernel::Syscalls
{
    void PeekNextEvent(SyscallRegisters& regs)
    {
        auto maybeEvent = Scheduling::ThreadGroup::Current()->PeekEvent();
        if (!maybeEvent)
        {
            regs.id = 1;
            regs.arg0 = 0;
            return;
        }

        regs.arg0 = (uint32_t)maybeEvent->type | ((uint64_t)maybeEvent->length << 32);
    }

    void ConsumeNextEvent(SyscallRegisters& regs)
    {
        auto maybeEvent = Scheduling::ThreadGroup::Current()->ConsumeEvent();
        if (!maybeEvent)
        {
            regs.id = 1;
            regs.arg0 = 0;
            return;
        }

        if (!Memory::VMM::Current()->RangeExists(regs.arg0, maybeEvent->length, Memory::MemoryMapFlags::UserAccessible))
        {
            regs.id = (uint64_t)np::Syscall::ProgramEventError::InvalidBufferRange;
            return;
        }

        //some event types require custom processing
        using Kernel::Scheduling::ThreadGroupEventType;
        switch (maybeEvent->type) 
        {
        case ThreadGroupEventType::IncomingMail:
            //the event data is the address of the mailbox control/ipc stream
            Memory::IpcManager::Global()->ReceiveMail(maybeEvent->address.As<Memory::IpcStream>(), { regs.arg0, maybeEvent->length});
            break;

        default:
            if (maybeEvent->length > 0 && regs.arg0 > 0)
                sl::memcopy(maybeEvent->address.ptr, (void*)regs.arg0, maybeEvent->length);
            break;
        }

        regs.arg0 = (uint32_t)maybeEvent->type | ((uint64_t)maybeEvent->length << 32);
    }

    void GetPendingEventCount(SyscallRegisters& regs)
    {
        regs.arg0 = Scheduling::ThreadGroup::Current()->PendingEventCount();
    }
}
