#include <memory/IpcManager.h>
#include <memory/VirtualMemory.h>
#include <memory/IpcMailbox.h>
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

    sl::Opt<IpcStream*> IpcManager::StartStream(const sl::String& name, size_t length, IpcStreamFlags flags, IpcAccessFlags accessFlags)
    {
        if (!sl::EnumHasFlag(flags, IpcStreamFlags::UseSharedMemory))
        {
            Log("IPC Streams currently only support using shared memory.", LogSeverity::Error);
            return {};
        }

        sl::ScopedSpinlock scopeLock(&lock);

        //check that this name is available
        for (size_t i = 0; i < streams->Size(); i++)
        {
            if (streams->At(i) == nullptr)
                continue;

            if (streams->At(i)->name == name)
            {
                Logf("Attempted to create duplicate stream of: %s", LogSeverity::Error, name.C_Str());
                return {};
            }
        }
        
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
        stream->accessFlags = accessFlags;
        stream->ownerId = Scheduling::ThreadGroup::Current()->Id();

        //we allocate the buffer initially in the host's memory space, any clients will simply point to the physical memory allocated here.
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
            if (!sl::EnumHasFlag(Scheduling::Thread::Current()->Flags(), Scheduling::ThreadFlags::KernelMode)
                && !sl::EnumHasFlag(flags, IpcStreamFlags::SuppressUserAccess))
                flags = flags | MemoryMapFlags::UserAccessible;
            
            IpcStream* existingStream = streams->At(i);

            //check if we are allowed to access this stream
            if (existingStream->accessFlags == IpcAccessFlags::Disallowed)
                return {};
            if (existingStream->accessFlags == IpcAccessFlags::Private || existingStream->accessFlags == IpcAccessFlags::SelectedOnly)
            {
                Scheduling::Thread* currentThread = Scheduling::Thread::Current();
                for (size_t index = 0; index < existingStream->accessList.Size(); index++)
                {
                    if (existingStream->accessList[index] == currentThread->Id() 
                        || existingStream->accessList[index] == currentThread->Parent()->Id())
                        goto access_allowed;
                }

                return {};
            }
access_allowed:

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

    sl::Opt<IpcStream*> IpcManager::GetStreamDetails(const sl::String& name)
    {
        sl::ScopedSpinlock scopeLock(&lock);

        for (size_t i = 0; i < streams->Size(); i++)
        {
            if (streams->At(i) == nullptr)
                continue;
            if (streams->At(i)->name != name)
                continue;
            
            return streams->At(i);
        }

        return {};
    }

    sl::Opt<IpcStream*> IpcManager::CreateMailbox(const sl::String& mailbox, IpcStreamFlags flags, IpcAccessFlags accessFlags)
    {
        if (!sl::EnumHasFlag(flags, IpcStreamFlags::UseSharedMemory))
        {
            Log("IPC mailboxes must use shared memory.", LogSeverity::Error);
            return {};
        }

        auto maybeStream = StartStream(mailbox, MailboxDefaultSize, flags, accessFlags);
        if (!maybeStream)
            return {};

        IpcMailboxControl* mailControl = maybeStream.Value()->bufferAddr.As<IpcMailboxControl>();
        mailControl->head = mailControl->tail = 0;
        sl::SpinlockRelease(&mailControl->lock);

        return *maybeStream;
    }

    void IpcManager::DestroyMailbox(const sl::String& mailbox)
    {
        //TODO: implement DestroyMailbox()
    }

    bool IpcManager::PostMail(const sl::String& destMailbox, const sl::BufferView data, bool keepOpenHint)
    {
        auto maybeDetails = GetStreamDetails(destMailbox);
        if (!maybeDetails)
            return false;

        const IpcStream* streamDetails = *maybeDetails;
        if (Scheduling::ThreadGroup::Current()->Id() == streamDetails->ownerId)
            return false; //no lookback support for now.

        //TODO: we're forcing use of shared memory, but not when the mailbox is created.
        auto maybeMailbox = OpenStream(destMailbox, sl::EnumSetFlag(IpcStreamFlags::UseSharedMemory, IpcStreamFlags::SuppressUserAccess));
        if (!maybeMailbox)
            return false;

        IpcMailboxControl* mailControl = maybeMailbox->As<IpcMailboxControl>();
        sl::ScopedSpinlock mailLock(&mailControl->lock);

        //determine where the next message will go
        IpcMailHeader* nextBlank = sl::NativePtr(mailControl + 1).As<IpcMailHeader>();
        if (mailControl->head > 0)
        {
            //there is data, jump to the end and check if there is enough space.
            sl::NativePtr scan = mailControl->Last()->Next();
            if (scan.raw + sizeof(IpcMailHeader) + data.length > streamDetails->bufferLength)
            {
                //TODO: we should implement this as a circular buffer
                return false; //buffer is full, cannot receive mail
            }
            nextBlank = scan.As<IpcMailHeader>();
        }

        nextBlank->length = data.length;
        nextBlank->positionInStream = sl::NativePtr(nextBlank).raw - sl::NativePtr(mailControl).raw;
        nextBlank->Next()->length = 0;
        sl::memcopy(data.base.ptr, nextBlank->data, data.length);

        mailControl->tail = nextBlank->positionInStream;
        if ((size_t)(mailControl + 1) == (size_t)nextBlank)
            mailControl->head = nextBlank->positionInStream;

        //send an event to the target process
        Scheduling::ThreadGroup* ownerGroup = Scheduling::Scheduler::Global()->GetThreadGroup(streamDetails->ownerId);
        if (ownerGroup == nullptr)
            return false;
        
        //the process will be expected addresses in *it's* address space, not ours.
        ownerGroup->PushEvent({ Scheduling::ThreadGroupEventType::IncomingMail, (uint32_t)data.length, (void*)streamDetails });

        //close the stream if user thinks that's wise
        if (!keepOpenHint)
            CloseStream(destMailbox);
        
        return true;
    }

    bool IpcManager::MailAvailable(const sl::String& mailbox)
    {
        auto maybeDetails = GetStreamDetails(mailbox);
        if (!maybeDetails)
            return false;
        
        IpcMailboxControl* mailControl = maybeDetails.Value()->bufferAddr.As<IpcMailboxControl>();
        sl::ScopedSpinlock scopeLock(&mailControl->lock);
        return mailControl->head > 0;
    }

    void IpcManager::ReceiveMail(IpcStream* hostStream, sl::BufferView receiveInto)
    {
        if (hostStream == nullptr)
            return;
        if (hostStream->ownerId != Scheduling::ThreadGroup::Current()->Id())
            return;
        
        IpcMailboxControl* mailControl = hostStream->bufferAddr.As<IpcMailboxControl>();
        sl::ScopedSpinlock mailLock(&mailControl->lock);
        IpcMailHeader* latestMail = mailControl->First();

        if (latestMail->length > receiveInto.length)
        {
            Log("Attempted to receive IPC mail into undersized buffer.", LogSeverity::Warning);
            return;
        }
        if (receiveInto.base.ptr == nullptr)
            return;
        
        //copy mail data if we have somewhere to put it
        if (receiveInto.base.ptr != nullptr && latestMail->length > 0)
            sl::memcopy(latestMail->data, receiveInto.base.ptr, latestMail->length);
        
        //update head + tail pointers
        if (latestMail->Next()->length > 0)
            mailControl->head = (size_t)latestMail->Next() - (size_t)mailControl;
        else
            mailControl->head = mailControl->tail = 0;
    }
}
