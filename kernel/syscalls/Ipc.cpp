#include <syscalls/Dispatch.h>
#include <memory/IpcManager.h>
#include <scheduling/Thread.h>
#include <SyscallEnums.h>

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
}
