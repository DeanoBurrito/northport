#include <syscalls/Dispatch.h>
#include <scheduling/Scheduler.h>
#include <memory/IpcMailbox.h>
#include <SyscallEnums.h>
#include <Locks.h>

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
    {
        auto maybeEvent = Scheduling::ThreadGroup::Current()->ConsumeEvent();
        if (!maybeEvent)
        {
            regs.arg0 = 0;
            return;
        }

        if (!Memory::VMM::Current()->RangeExists(regs.arg0, maybeEvent->length, Memory::MemoryMapFlags::UserAccessible))
        {
            regs.id = (uint64_t)np::Syscall::ProgramEventError::InvalidBufferRange;
            return;
        }

        if (maybeEvent->length > 0 && regs.arg0 > 0)
            sl::memcopy(maybeEvent->address.ptr, (void*)regs.arg0, maybeEvent->length);

        //some event types require further processing
        using Kernel::Scheduling::ThreadGroupEventType;
        switch (maybeEvent->type) 
        {
        case ThreadGroupEventType::IncomingMail:
        {
            if (maybeEvent->length == 0)
                break;
            
            Memory::IpcMailboxControl* mailControl = maybeEvent->address.As<Memory::IpcMailHeader>()->Control();
            sl::ScopedSpinlock scopeLock(&mailControl->lock);
            Memory::IpcMailHeader* next = mailControl->First(); 
            if (next->Next()->length > 0)
                mailControl->head = (size_t)next->Next() - maybeEvent->address.raw;
            else
                mailControl->head = mailControl->tail = 0;

            break;
        }

        default:
            break;
        }

        regs.arg0 = (uint32_t)maybeEvent->type | ((uint64_t)maybeEvent->length << 32);
    }

    void GetPendingEventCount(SyscallRegisters& regs)
    {
        regs.arg0 = Scheduling::ThreadGroup::Current()->PendingEventCount();
    }
}
