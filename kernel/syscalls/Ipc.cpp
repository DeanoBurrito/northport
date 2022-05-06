#include <syscalls/Dispatch.h>
#include <memory/IpcManager.h>
#include <memory/IpcMailbox.h>
#include <scheduling/Thread.h>
#include <scheduling/Scheduler.h>
#include <SyscallEnums.h>
#include <Locks.h>
#include <Log.h>

namespace Kernel::Syscalls
{
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
        using namespace Memory;
        if (!VMM::Current()->RangeExists(regs.arg0, PAGE_FRAME_SIZE))
        {
            regs.id = (uint64_t)np::Syscall::IpcError::InvalidBufferRange;
            return;
        }

        IpcAccessFlags accessFlags = (IpcAccessFlags)(regs.arg1 >> 60);
        regs.arg1 = regs.arg1 & ~((uint64_t)0xF << 60);

        sl::String mailboxName(sl::NativePtr(regs.arg0).As<const char>());
        auto maybeMailbox = IpcManager::Global()->CreateMailbox(mailboxName, (IpcStreamFlags)regs.arg1, accessFlags);
        if (!maybeMailbox)
        {
            regs.id = (uint64_t)np::Syscall::IpcError::StreamStartFail;
            return;
        }

        using namespace Scheduling;
        auto maybeResId = ThreadGroup::Current()->AttachResource(ThreadResourceType::IpcMailbox, *maybeMailbox);
        if (!maybeResId)
        {
            IpcManager::Global()->DestroyMailbox(mailboxName);
            regs.id = (uint64_t)np::Syscall::IpcError::NoResourceId;
            return;
        }

        regs.arg0 = *maybeResId;
    }

    void DestroyMailbox(SyscallRegisters& regs)
    {
        //scrub incoming data, and call IpcManager::DestroyMailbox
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

        const bool leaveOpenHint = (regs.arg3 != 0);
        sl::String mailboxName(sl::NativePtr(regs.arg0).As<const char>());
        if (!IpcManager::Global()->PostMail(mailboxName, {regs.arg1, regs.arg2}, leaveOpenHint))
        {
            regs.id = (uint64_t)np::Syscall::IpcError::MailDeliveryFailed;
            return;
        }
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
