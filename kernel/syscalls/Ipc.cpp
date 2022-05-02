#include <syscalls/Dispatch.h>
#include <memory/IpcManager.h>
#include <scheduling/Thread.h>
#include <scheduling/Scheduler.h>
#include <SyscallEnums.h>
#include <Locks.h>
#include <Log.h>

namespace Kernel::Syscalls
{
    struct IpcMailHeader
    {
        uint64_t length;
        uint8_t data[];

        IpcMailHeader* Next()
        { return sl::NativePtr(this).As<IpcMailHeader>(length); }
    };

    struct IpcMailboxControl
    {
        uint8_t lock;
        uint8_t reserved0[7];
        uint64_t head;
        uint64_t tail;
        uint64_t reserved1;

        IpcMailHeader* First()
        { return sl::NativePtr(this).As<IpcMailHeader>(head); }

        IpcMailHeader* Last()
        { return sl::NativePtr(this).As<IpcMailHeader>(tail); }
    };
    
    constexpr size_t MailboxDefaultSize = 0x1000;

    void StartIpcStream(SyscallRegisters& regs)
    {
        using namespace Memory;

        if (!VMM::Current()->RangeExists(regs.arg0, PAGE_FRAME_SIZE))
        {
            //this can fail either because the memory isnt mapped, or because the string is greater than 1 page long (if so, probably not a valid request anyway).
            regs.id = (uint64_t)np::Syscall::IpcError::InvalidBufferRange;
            return;
        }

        //we'll need to separate the access flags (upper 4 bits) from the incoming flags
        IpcAccessFlags accessFlags = (IpcAccessFlags)(regs.arg1 >> 60);
        regs.arg1 = regs.arg1 & ~((uint64_t)0xF << 60);

        //try to start an ipc stream
        sl::String streamName(sl::NativePtr(regs.arg0).As<const char>());
        auto maybeStream = IpcManager::Global()->StartStream(streamName, regs.arg2, (IpcStreamFlags)regs.arg1, accessFlags);
        if (!maybeStream)
        {
            regs.id = (uint64_t)np::Syscall::IpcError::StreamStartFail;
            return;
        }

        //attach the stream as a thread resource, return details as per spec
        auto maybeResId = Scheduling::ThreadGroup::Current()->AttachResource(Scheduling::ThreadResourceType::IpcStream, *maybeStream);
        if (!maybeResId)
        {
            IpcManager::Global()->StopStream(sl::String(regs.arg0));
            regs.id = (uint64_t)np::Syscall::IpcError::NoResourceId;
            return;
        }

        regs.arg0 = *maybeResId;
        if (sl::EnumHasFlag(regs.arg1, IpcStreamFlags::UseSharedMemory))
            regs.arg2 = maybeStream.Value()->bufferAddr.raw;
        else
            regs.arg2 = 0;
        regs.arg1 = maybeStream.Value()->bufferLength;
    }

    void StopIpcStream(SyscallRegisters& regs)
    {
        using namespace Memory;

        auto maybeResource = Scheduling::ThreadGroup::Current()->GetResource(regs.arg0);
        if (!maybeResource)
        {
            regs.id = (uint64_t)np::Syscall::IpcError::NoResourceId;
            return;
        }

        //NOTE: this seems a bit wasteful to do a lookup in ipcmanager when we have a pointer to the stream right here.
        IpcStream* stream = maybeResource.Value()->res.As<IpcStream>();
        Scheduling::ThreadGroup::Current()->DetachResource(regs.arg0);
        IpcManager::Global()->StopStream(stream->name);
    }

    void OpenIpcStream(SyscallRegisters& regs)
    {
        using namespace Memory;

        if (!VMM::Current()->RangeExists(regs.arg0, PAGE_FRAME_SIZE))
        {
            regs.id = (uint64_t)np::Syscall::IpcError::InvalidBufferRange;
            return;
        }

        //try to connect to an existing stream
        sl::String streamName(sl::NativePtr(regs.arg0).As<const char>());
        auto maybeStream = IpcManager::Global()->OpenStream(streamName, (IpcStreamFlags)regs.arg1);
        if (!maybeStream)
        {
            regs.id = (uint64_t)np::Syscall::IpcError::StreamStartFail;
            return;
        }

        auto maybeResId = Scheduling::ThreadGroup::Current()->AttachResource(Scheduling::ThreadResourceType::IpcStream, *maybeStream);
        if (!maybeResId)
        {
            IpcManager::Global()->CloseStream(streamName);
            regs.id = (uint64_t)np::Syscall::IpcError::NoResourceId;
            return;
        }

        regs.arg0 = *maybeResId;
        if (sl::EnumHasFlag(regs.arg1, IpcStreamFlags::UseSharedMemory))
            regs.arg2 = maybeStream->raw;
        else
            regs.arg2 = 0;
        regs.arg1 = 0; //TODO: fetch info about the stream, like its size. We probably need a rewrite of the ipc stream API anyway. A lot has been learnt.
    }

    void CloseIpcStream(SyscallRegisters& regs)
    {
        using namespace Memory;
        
        auto maybeResource = Scheduling::ThreadGroup::Current()->GetResource(regs.arg0);
        if (!maybeResource)
        {
            regs.id = (uint64_t)np::Syscall::IpcError::NoResourceId;
            return;
        }

        IpcStream* stream = maybeResource.Value()->res.As<IpcStream>();
        Scheduling::ThreadGroup::Current()->DetachResource(regs.arg0);
        IpcManager::Global()->CloseStream(stream->name);
    }

    void CreateMailbox(SyscallRegisters& regs)
    {
        //the first section is just setting up an ipc stream. 
        using namespace Memory;
        
        if (!VMM::Current()->RangeExists(regs.arg0, PAGE_FRAME_SIZE))
        {
            regs.id = (uint64_t)np::Syscall::IpcError::InvalidBufferRange;
            return;
        }

        IpcAccessFlags accessFlags = (IpcAccessFlags)(regs.arg1 >> 60);
        regs.arg1 = regs.arg1 & ~((uint64_t)0xF << 60);

        sl::String streamName(sl::NativePtr(regs.arg0).As<const char>());
        auto maybeStream = IpcManager::Global()->StartStream(streamName, MailboxDefaultSize, (IpcStreamFlags)regs.arg1, accessFlags);
        if (!maybeStream)
        {
            regs.id = (uint64_t)np::Syscall::IpcError::StreamStartFail;
            return;
        }

        using namespace Scheduling;
        auto maybeResId = ThreadGroup::Current()->AttachResource(ThreadResourceType::IpcMailbox, *maybeStream);
        if (!maybeResId)
        {
            IpcManager::Global()->StopStream(streamName);
            regs.id = (uint64_t)np::Syscall::IpcError::NoResourceId;
            return;
        }

        //we reserve 4x uint64s at the beginning (lock, head, tail and a reserve u64), and ensure lock is cleared
        sl::memsetT<uint64_t>(maybeStream.Value()->bufferAddr.ptr, 0, 4);
        sl::SpinlockRelease(maybeStream.Value()->bufferAddr.ptr);

        regs.arg0 = *maybeResId;
    }

    void DestroyMailbox(SyscallRegisters& regs)
    {
        //remove any pending mail events, and remove any pending mail data
        
        //close the stream
    }

    void PostToMailbox(SyscallRegisters& regs)
    {
        using namespace Memory;
        //usual safety checks, ensure the string's buffer and incoming data buffers are valid memory regions
        if (!VMM::Current()->RangeExists(regs.arg0, PAGE_FRAME_SIZE))
        {
            regs.id = (uint64_t)np::Syscall::IpcError::InvalidBufferRange;
            return;
        }
        if (regs.arg2 > 0 && !VMM::Current()->RangeExists(regs.arg1, regs.arg2))
        {
            regs.id = (uint64_t)np::Syscall::IpcError::InvalidBufferRange;
            return;
        }

        //try to access our destination
        sl::String streamName(sl::NativePtr(regs.arg0).As<const char>());
        //TODO: we're forcing the use of shared memory here, we should also force it in mailbox creation too.
        auto maybeStream = IpcManager::Global()->OpenStream(streamName, IpcStreamFlags::UseSharedMemory);
        if (!maybeStream)
        {
            regs.id = (uint64_t)np::Syscall::IpcError::MailDeliveryFailed;
            return;
        }
        IpcMailboxControl* mailControl = maybeStream->As<IpcMailboxControl>();

        /*  TODO: decide on a policy here.
        /   Opening/Closing a stream is an expensive operation, especially for something small like sending a message.
        /   For the moment I'm going to leave the stream open, but for a process that only makes a few requests this is kind of annoying.
        /   It's another linked process accessing shared memory that dosnt need to be.
        /   For now I've chosen the speedy approach of opening the stream once,
        /   and then checking if its exists on subsequent sends. I would like to come up with a better solution in the future though.
        /
        /   Perhaps we could include a flag in the syscall that hints as whether this is a single call,
        /   or part of a series of multiple calls that are occuring. "KeepAlive"?
        */

        {
            //lock the mailbox, and copy the data into it, updating the pointers as we go.
            sl::ScopedSpinlock streamLock(&mailControl->lock);
            sl::NativePtr mailBuffer = mailControl + 1;

            if (mailControl->head > 0)
            {
                IpcMailHeader* scan = mailControl->First();
                while (scan != mailControl->Last())
                    scan = scan->Next();
                mailBuffer = scan->Next();
                //TODO: we never check if there is actually enough buffer space to send this mail
                //We'll need more details like the stream size for this
            }

            mailBuffer.As<IpcMailHeader>()->length = regs.arg2;
            sl::memcopy((void*)regs.arg1, mailBuffer.As<IpcMailHeader>()->data, regs.arg2);

            //for head and tail we store relative offsets to the stream base
            if (mailBuffer.raw == (uint64_t)(mailControl + 1))
                mailControl->head = sizeof(IpcMailboxControl);
            mailControl->tail = mailBuffer.raw - maybeStream->raw;
        }

        const IpcStream* streamDetails = *IpcManager::Global()->GetStreamDetails(streamName);
        //post an event to the target processs
        Scheduling::ThreadGroup* ownerGroup = Scheduling::Scheduler::Global()->GetThreadGroup(streamDetails->ownerId);
        if (ownerGroup == nullptr)
        {
            Log("IPC send mail failed as owning thread could not recieve event.", LogSeverity::Warning);
            return;
        }

        ownerGroup->PushEvent({Scheduling::ThreadGroupEventType::IncomingMail, (uint32_t)regs.arg2, regs.arg1 });
    }

    void ModifyIpcConfig(SyscallRegisters& regs)
    {
        using namespace Memory;
        using np::Syscall::IpcConfigOperation;
        auto maybeResource = Scheduling::ThreadGroup::Current()->GetResource(regs.arg1);

        if (!maybeResource)
            return;
        IpcStream* stream = maybeResource.Value()->res.As<IpcStream>();
        
        //TODO: locking on individual streams. We're messing with the internals here without ensuring exclusivity.
        IpcConfigOperation op = (IpcConfigOperation)regs.arg0;
        switch (op)
        {
        case IpcConfigOperation::AddAccessId:
        {
            for (size_t i = 0; i < stream->accessList.Size(); i++)
            {
                if (stream->accessList[i] == regs.arg2)
                    return;
            }
            
            stream->accessList.PushBack(regs.arg2);
            return;
        }

        case IpcConfigOperation::RemoveAccessId:
        {
            for (size_t i = 0; i < stream->accessList.Size(); i++)
            {
                if (stream->accessList[i] == regs.arg2)
                {
                    stream->accessList.Erase(i);
                    break;
                }
            }
            return;
        }

        case IpcConfigOperation::ChangeAccessFlags:
            stream->accessFlags = (IpcAccessFlags)(regs.arg2 >> 60);
            return;

        case IpcConfigOperation::TransferOwnership:
            //TODO: we probably want to check this new process exists, and if not assume the stream to be abandoned and clean it up.
            stream->ownerId = regs.arg2;
            return;

        default:
            return;
        }
    }
}
