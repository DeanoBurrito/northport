#include <syscalls/Dispatch.h>
#include <scheduling/Scheduler.h>
#include <memory/IpcMailbox.h>
#include <memory/IpcManager.h>
#include <SyscallEnums.h>
#include <Vectors.h>
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

        regs.arg1 = 0;
        regs.arg2 = 0;
        using Kernel::Scheduling::ThreadGroupEventType;
        switch (maybeEvent->type)
        {
        case ThreadGroupEventType::IncomingMail:
        {
            auto maybeRid = Scheduling::ThreadGroup::Current()->FindResource(maybeEvent->address);
            if (maybeRid)
                regs.arg1 = *maybeRid;
            break;
        }
        default:
            break;
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

        if (!Memory::VMM::Current()->RangeExists({ regs.arg0, maybeEvent->length, Memory::MemoryMapFlags::UserAccessible }))
        {
            regs.id = (uint64_t)np::Syscall::ProgramEventError::InvalidBufferRange;
            return;
        }

        //some event types require custom processing:
        /*
            One particular thing I've done for events that return a small (< 8 bytes) of data is to put it in the address field of the event.
            Meaning the length is still how much data there is, but the address field *is* the data. Means no need to worry about managing
            a buffer elsewhere. This works great for key and mouse inputs.
        */
        CPU::AllowSumac(true);
        using Kernel::Scheduling::ThreadGroupEventType;
        switch (maybeEvent->type) 
        {
        case ThreadGroupEventType::IncomingMail:
        {
            //the event data is the address of the mailbox control/ipc stream
            auto maybeSenderId = Memory::IpcManager::Global()->ReceiveMail(maybeEvent->address.As<Memory::IpcStream>(), { regs.arg0, maybeEvent->length });
            auto maybeRid = Scheduling::ThreadGroup::Current()->FindResource(maybeEvent->address);
            if (maybeRid && maybeSenderId)
            {
                regs.arg1 = *maybeRid;
                regs.arg2 = *maybeSenderId;
            }
            else
            {
                regs.arg2 = regs.arg1 = 0;
                regs.id = 1; //TODO: actual error code,
                return;
            }
            break;
        }

        case ThreadGroupEventType::KeyEvent:
            if (regs.arg0 > 0)
                *sl::NativePtr(regs.arg0).As<uint64_t>() = maybeEvent->address.raw;
            break;

        case ThreadGroupEventType::MouseEvent:
            if (regs.arg0 > 0)
                *sl::NativePtr(regs.arg0).As<sl::Vector2i>() = { (int32_t)maybeEvent->address.raw, (int32_t)(maybeEvent->address.raw >> 32)};
            break;

        default:
            if (maybeEvent->length > 0 && regs.arg0 > 0)
                sl::memcopy(maybeEvent->address.ptr, (void*)regs.arg0, maybeEvent->length);
            break;
        }
        CPU::AllowSumac(false);

        regs.arg0 = (uint32_t)maybeEvent->type | ((uint64_t)maybeEvent->length << 32);
    }

    void GetPendingEventCount(SyscallRegisters& regs)
    {
        regs.arg0 = Scheduling::ThreadGroup::Current()->PendingEventCount();
    }
}
