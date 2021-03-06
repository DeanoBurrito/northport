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

        stream->flags = flags;
        stream->accessFlags = accessFlags;
        stream->ownerId = Scheduling::ThreadGroup::Current()->Id();

        //we allocate the buffer initially in the host's memory space, any clients will simply point to the physical memory allocated here.
        stream->buffer = VMM::Current()->AllocRange(pageCount * PAGE_FRAME_SIZE, true, mappingFlags).ToView();
        if (stream->buffer.base.ptr == nullptr)
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

            //tell any clients that we're closing the stream and to gtfo
            for (size_t j = 0; j < stream->clients.Size(); j++)
            {
                if (stream->clients[j].callback == nullptr)
                    continue;
                
                stream->clients[j].callback(stream, &stream->clients[j]);
            }

            streams->At(i) = nullptr;
            idAlloc->Free(i);

            VMM::Current()->RemoveRange({ stream->buffer.base.raw, stream->buffer.length });
            delete stream;
            return;
        }

        Logf("Could not close IPC stream, no stream exists with the name: %s", LogSeverity::Error, name.C_Str());
    }

    sl::Opt<sl::BufferView> IpcManager::OpenStream(const sl::String& name, IpcStreamFlags flags, const StreamCleanupCallback& callback)
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
            
            MemoryMapFlags memFlags = MemoryMapFlags::SystemRegion | MemoryMapFlags::AllowWrites;
            if (!sl::EnumHasFlag(Scheduling::Thread::Current()->Flags(), Scheduling::ThreadFlags::KernelMode)
                && !sl::EnumHasFlag(flags, IpcStreamFlags::SuppressUserAccess))
                memFlags = memFlags | MemoryMapFlags::UserAccessible;
            
            IpcStream* existingStream = streams->At(i);

            //check if the stream is already open, return it if so
            const size_t currentTgId = Scheduling::ThreadGroup::Current()->Id();
            for (size_t j = 0; j < existingStream->clients.Size(); j++)
            {
                if (existingStream->clients[j].threadGroupId != currentTgId)
                    continue;

                //we're already connected, return the current mapping.
                return existingStream->clients[j].localMapping;
            }

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
            //we're allowed to access the stream, map the physical pages, and add ourselves to the connected list
            auto foreignVmm = Scheduling::Scheduler::Global()->GetThreadGroup(existingStream->ownerId).Value()->VMM();
            const sl::BufferView range = VMM::Current()->AddSharedRange(
                    *foreignVmm, 
                    { existingStream->buffer.base.raw, existingStream->buffer.length, memFlags }
                ).ToView();
            
            existingStream->clients.PushBack({ currentTgId, range, callback });
            
            return range;
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

        IpcMailboxControl* mailControl = maybeStream.Value()->buffer.base.As<IpcMailboxControl>();
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
        //TODO: what happens if stream closes while we're posting mail? We'd need some kind of cancellation token. Or just a lock?
        auto maybeMailbox = OpenStream(destMailbox, sl::EnumSetFlag(IpcStreamFlags::UseSharedMemory, IpcStreamFlags::SuppressUserAccess), nullptr);
        if (!maybeMailbox)
            return false;

        IpcMailboxControl* mailControl = maybeMailbox->base.As<IpcMailboxControl>();
        sl::ScopedSpinlock mailLock(&mailControl->lock);

        //determine where the next message will go
        IpcMailHeader* nextBlank = nullptr;
        if (mailControl->tail == 0)
        {
            sl::NativePtr nextAddr = mailControl + 1;
            nextAddr.raw = (nextAddr.raw / sizeof(IpcMailHeader) + 1) * sizeof(IpcMailHeader);
            nextBlank = nextAddr.As<IpcMailHeader>();
        }
        else
            nextBlank = mailControl->Last()->Next();

        nextBlank->length = data.length;
        nextBlank->positionInStream = sl::NativePtr(nextBlank).raw - sl::NativePtr(mailControl).raw;
        if (nextBlank->positionInStream + sizeof(IpcMailHeader) + data.length > streamDetails->buffer.length)
            return false; //not enough space in stream.

        nextBlank->Next()->length = 0;
        nextBlank->sender = Scheduling::ThreadGroup::Current()->Id();
        sl::memcopy(data.base.ptr, nextBlank->data, data.length);

        mailControl->tail = nextBlank->positionInStream;
        if (mailControl->head == 0)
            mailControl->head = nextBlank->positionInStream;

        //send an event to the target process
        Scheduling::ThreadGroup* ownerGroup = Scheduling::Scheduler::Global()->GetThreadGroup(streamDetails->ownerId).Value();
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
        
        IpcMailboxControl* mailControl = maybeDetails.Value()->buffer.base.As<IpcMailboxControl>();
        sl::ScopedSpinlock scopeLock(&mailControl->lock);
        return mailControl->head > 0;
    }

    sl::Opt<uint64_t> IpcManager::ReceiveMail(IpcStream* hostStream, sl::BufferView receiveInto)
    {
        if (hostStream == nullptr)
            return {};
        if (hostStream->ownerId != Scheduling::ThreadGroup::Current()->Id())
            return {};
        
        IpcMailboxControl* mailControl = hostStream->buffer.base.As<IpcMailboxControl>();
        sl::ScopedSpinlock mailLock(&mailControl->lock);
        if (mailControl->head == 0)
            return {};

        IpcMailHeader* latestMail = mailControl->First();

        if (latestMail->length > receiveInto.length)
        {
            Log("Attempted to receive IPC mail into undersized buffer.", LogSeverity::Warning);
            return {};
        }
        if (receiveInto.base.ptr == nullptr)
            return {};
        
        //copy mail data if we have somewhere to put it
        if (latestMail->length > 0)
            sl::memcopy(latestMail->data, receiveInto.base.ptr, latestMail->length);
        
        //update head + tail pointers
        if (latestMail->positionInStream != mailControl->tail)
            mailControl->head = latestMail->Next()->positionInStream;
        else
            mailControl->head = mailControl->tail = 0;
        
        return latestMail->sender;
    }
}
