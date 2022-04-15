#include <memory/IpcManager.h>
#include <memory/VirtualMemory.h>
#include <scheduling/Thread.h>
#include <scheduling/Scheduler.h>
#include <Platform.h>
#include <Locks.h>
#include <Log.h>

#define IPC_STREAM_START_CAPACITY 128

namespace Kernel::Memory
{
    IpcManager globalIpcManager;
    IpcManager* IpcManager::Global()
    { return &globalIpcManager; }
    
    void IpcManager::Init()
    {
        //pre-allocate space for a bunch of streams, so save thrashing the heap early on
        streams = new sl::Vector<IpcStream*>();
        idAlloc = new sl::UIdAllocator();
        streams->EnsureCapacity(IPC_STREAM_START_CAPACITY);
        for (size_t i = 0; i < streams->Capacity(); i++)
            streams->EmplaceBack();
        sl::SpinlockRelease(&lock);
        
        Log("Global IPC manager initialized.", LogSeverity::Info);
    }

    sl::Opt<IpcStream*> IpcManager::StartStream(const sl::String& name, size_t length, IpcStreamFlags flags)
    {
        if (!sl::EnumHasFlag(flags, IpcStreamFlags::UseSharedMemory))
        {
            Log("IPC Streams currently only support using shared memory.", LogSeverity::Error);
            return {};
        }

        sl::ScopedSpinlock scopeLock(&lock);
        
        const size_t pageCount = (length / PAGE_FRAME_SIZE + 1);
        const size_t streamId = idAlloc->Alloc();

        while (streamId > streams->Size())
            streams->EmplaceBack();
        
        IpcStream* stream = new IpcStream(name);
        streams->At(streamId) = stream;
        MemoryMapFlags mappingFlags = MemoryMapFlags::AllowWrites | MemoryMapFlags::SystemRegion;
        if (!sl::EnumHasFlag(Scheduling::Thread::Current()->Flags(), Scheduling::ThreadFlags::KernelMode))
            mappingFlags = mappingFlags | MemoryMapFlags::UserAccessible;

        stream->bufferLength = pageCount * PAGE_FRAME_SIZE;
        stream->flags = flags;
        stream->ownerId = Scheduling::ThreadGroup::Current()->Id();
        //we allocate the buffer initially in the host's memory space, any clients will simply point to the physical memory allocate here.
        stream->bufferAddr = VMM::Current()->AllocateRange(pageCount * PAGE_FRAME_SIZE, mappingFlags);
        if (stream->bufferAddr.ptr == nullptr)
        {
            //we failed to allocate a VM region for whatever reason, undo anything we just updated.
            streams->At(streamId) = nullptr;
            idAlloc->Free(streamId);
            delete stream;

            return {};
        }

        return stream;
    }

    void IpcManager::StopStream(const sl::String& name)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        
        for (size_t i = 0; i < streams->Size(); i++)
        {
            if (streams->At(i) == nullptr)
                continue;

            if (streams->At(i)->name != name)
                continue; //this isnt too awful as sl::String::!= uses short circuit logic based on the string lengths first.

            IpcStream* stream = streams->At(i);
            streams->At(i) = nullptr;
            idAlloc->Free(i);

            //TODO: we'll need to tell any other buffers that are connected to this one that the stream has been broken.
            VMM::Current()->RemoveRange(stream->bufferAddr.raw, stream->bufferLength);
            delete stream;
            return;
        }

        Logf("Could not close IPC stream, no stream exists with the name: %s", LogSeverity::Error, name.C_Str());
    }

    sl::Opt<sl::NativePtr> IpcManager::OpenStream(const sl::String& name, IpcStreamFlags flags)
    {
        if (!sl::EnumHasFlag(flags, IpcStreamFlags::UseSharedMemory))
        {
            Log("IPC Streams currently only support using shared memory.", LogSeverity::Error);
            return {};
        }

        sl::ScopedSpinlock scopeLock(&lock);

        for (size_t i = 0; i < streams->Size(); i++)
        {
            if (streams->At(i) == nullptr)
                continue;
            if (streams->At(i)->name != name)
                continue;
            
            MemoryMapFlags flags = MemoryMapFlags::SystemRegion | MemoryMapFlags::AllowWrites;
            if (!sl::EnumHasFlag(Scheduling::Thread::Current()->Flags(), Scheduling::ThreadFlags::KernelMode))
                flags = flags | MemoryMapFlags::UserAccessible;
            
            IpcStream* existingStream = streams->At(i);
            const sl::NativePtr base = Scheduling::ThreadGroup::Current()->VMM()->AddSharedPhysicalRange(
                Scheduling::Scheduler::Global()->GetThreadGroup(existingStream->ownerId)->VMM(), 
                existingStream->bufferAddr, 
                existingStream->bufferLength,
                flags);
            
            return base;
        }

        return {};
    }

    void IpcManager::CloseStream(const sl::String& name)
    {
        //TODO: imeplement CloseStream()
    }
}
