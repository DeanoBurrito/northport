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
    using np::Syscall::GeneralError;
    using np::Syscall::IpcError;
    
    void StartIpcStream(SyscallRegisters& regs)
    {
        using namespace Memory;

        if (!VMM::Current()->RangeExists({ regs.arg0, PAGE_FRAME_SIZE }))
        {
            //this can fail either because the memory isnt mapped, or because the string is greater than 1 page long (if so, probably not a valid request anyway).
            regs.id = (uint64_t)GeneralError::InvalidBufferRange;
            return;
        }

        //we'll need to separate the access flags (upper 4 bits) from the incoming flags
        IpcAccessFlags accessFlags = (IpcAccessFlags)(regs.arg1 >> 60);
        regs.arg1 = regs.arg1 & ~((uint64_t)0xF << 60);

        //try to start an ipc stream
        CPU::AllowSumac(true);
        sl::String streamName(sl::NativePtr(regs.arg0).As<const char>());
        CPU::AllowSumac(false);

        auto maybeStream = IpcManager::Global()->StartStream(streamName, regs.arg2, (IpcStreamFlags)regs.arg1, accessFlags);
        if (!maybeStream)
        {
            regs.id = (uint64_t)IpcError::StreamStartFail;
            return;
        }

        //attach the stream as a thread resource, return details as per spec
        auto maybeResId = Scheduling::ThreadGroup::Current()->AttachResource(Scheduling::ThreadResourceType::IpcStream, *maybeStream);
        if (!maybeResId)
        {
            
            IpcManager::Global()->StopStream(streamName);
            regs.id = (uint64_t)GeneralError::BadHandleId;
            return;
        }

        regs.arg0 = *maybeResId;
        if (sl::EnumHasFlag(regs.arg1, IpcStreamFlags::UseSharedMemory))
            regs.arg2 = maybeStream.Value()->buffer.base.raw;
        else
            regs.arg2 = 0;
        regs.arg1 = maybeStream.Value()->buffer.length;
    }

    void StopIpcStream(SyscallRegisters& regs)
    {
        using namespace Memory;

        auto maybeResource = Scheduling::ThreadGroup::Current()->GetResource(regs.arg0);
        if (!maybeResource)
        {
            regs.id = (uint64_t)GeneralError::BadHandleId;
            return;
        }

        IpcStream* stream = maybeResource.Value()->res.As<IpcStream>();
        Scheduling::ThreadGroup::Current()->DetachResource(regs.arg0);
        IpcManager::Global()->StopStream(stream->name);
    }

    void HandleStreamClosing(const Memory::IpcStream* stream, const Memory::IpcStreamClient* client)
    {
        //NOTE: we can't assume the context this callback will be run in. It may not run inside the same
        //address space as the client we're cleaning up.
        
        Scheduling::ThreadGroup* tg = *Scheduling::Scheduler::Global()->GetThreadGroup(client->threadGroupId);
        if (tg == nullptr)
        {
            Logf("Weird state when closing ipc stream: threadgroup (id=%u) marked as connected, but does not exist.", 
                LogSeverity::Warning, client->threadGroupId);
            return;
        }

        auto maybeRes = tg->FindResource(client->localMapping.base);
        if (!maybeRes)
        {
            Logf("Weird state when closing ipc stream: threadgroup (id=%u) has no handle, even though it is marked as connected.", 
                LogSeverity::Warning, client->threadGroupId);
        }
        else
        {
            //we have a valid rid, detach the resource
            tg->DetachResource(*maybeRes);
        }
        
        //remove the range from the vmm.
        if (!tg->VMM()->RemoveRange({ client->localMapping.base.raw, client->localMapping.length }) 
            && sl::EnumHasFlag(stream->flags, Memory::IpcStreamFlags::UseSharedMemory))
        {
            Logf("Error closing ipc stream: failed to remove client (threadgroup=%u) vmrange into shared physical memory.", 
                LogSeverity::Error, client->threadGroupId);
        }
    }

    void OpenIpcStream(SyscallRegisters& regs)
    {
        using namespace Memory;

        if (!VMM::Current()->RangeExists({ regs.arg0, PAGE_FRAME_SIZE }))
        {
            regs.id = (uint64_t)GeneralError::InvalidBufferRange;
            return;
        }

        //try to connect to an existing stream
        CPU::AllowSumac(true);
        sl::String streamName(sl::NativePtr(regs.arg0).As<const char>());
        CPU::AllowSumac(false);

        auto maybeStream = IpcManager::Global()->OpenStream(streamName, (IpcStreamFlags)regs.arg1, HandleStreamClosing);
        if (!maybeStream)
        {
            regs.id = (uint64_t)IpcError::StreamStartFail;
            return;
        }

        auto maybeResId = Scheduling::ThreadGroup::Current()->AttachResource(Scheduling::ThreadResourceType::IpcStream, maybeStream->base);
        if (!maybeResId)
        {
            IpcManager::Global()->CloseStream(streamName);
            regs.id = (uint64_t)GeneralError::BadHandleId;
            return;
        }

        regs.arg0 = *maybeResId;
        if (sl::EnumHasFlag(regs.arg1, IpcStreamFlags::UseSharedMemory))
            regs.arg2 = maybeStream->base.raw;
        else
            regs.arg2 = 0;
        regs.arg1 = maybeStream->length;
    }

    void CloseIpcStream(SyscallRegisters& regs)
    {
        using namespace Memory;
        
        auto maybeResource = Scheduling::ThreadGroup::Current()->GetResource(regs.arg0);
        if (!maybeResource)
        {
            regs.id = (uint64_t)GeneralError::BadHandleId;
            return;
        }

        IpcStream* stream = maybeResource.Value()->res.As<IpcStream>();
        Scheduling::ThreadGroup::Current()->DetachResource(regs.arg0);
        IpcManager::Global()->CloseStream(stream->name);
    }

    void CreateMailbox(SyscallRegisters& regs)
    {
        using namespace Memory;
        if (!VMM::Current()->RangeExists({ regs.arg0, PAGE_FRAME_SIZE }))
        {
            regs.id = (uint64_t)GeneralError::InvalidBufferRange;
            return;
        }

        IpcAccessFlags accessFlags = (IpcAccessFlags)(regs.arg1 >> 60);
        regs.arg1 = regs.arg1 & ~((uint64_t)0xF << 60);

        CPU::AllowSumac(true);
        sl::String mailboxName(sl::NativePtr(regs.arg0).As<const char>());
        auto maybeMailbox = IpcManager::Global()->CreateMailbox(mailboxName, (IpcStreamFlags)regs.arg1, accessFlags);
        CPU::AllowSumac(false);
        if (!maybeMailbox)
        {
            regs.id = (uint64_t)IpcError::StreamStartFail;
            return;
        }

        using namespace Scheduling;
        auto maybeResId = ThreadGroup::Current()->AttachResource(ThreadResourceType::IpcMailbox, *maybeMailbox);
        if (!maybeResId)
        {
            IpcManager::Global()->DestroyMailbox(mailboxName);
            regs.id = (uint64_t)GeneralError::BadHandleId;
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
        if (!VMM::Current()->RangeExists({ regs.arg0, PAGE_FRAME_SIZE }))
        {
            regs.id = (uint64_t)GeneralError::InvalidBufferRange;
            return;
        }
        if (regs.arg2 > 0 && !VMM::Current()->RangeExists({ regs.arg1, regs.arg2 }))
        {
            regs.id = (uint64_t)GeneralError::InvalidBufferRange;
            return;
        }

        const bool leaveOpenHint = (regs.arg3 != 0);
        CPU::AllowSumac(true);
        sl::String mailboxName(sl::NativePtr(regs.arg0).As<const char>());
        if (!IpcManager::Global()->PostMail(mailboxName, {regs.arg1, regs.arg2}, leaveOpenHint))
        {
            regs.id = (uint64_t)IpcError::MailDeliveryFailed;
            return;
        }
        CPU::AllowSumac(false);
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
