#include <syscalls/Dispatch.h>
#include <memory/IpcManager.h>
#include <scheduling/Thread.h>
#include <SyscallEnums.h>

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

        //try to start an ipc stream
        sl::String streamName(sl::NativePtr(regs.arg0).As<const char>());
        auto maybeStream = IpcManager::Global()->StartStream(streamName, regs.arg2, (IpcStreamFlags)regs.arg1);
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
}
