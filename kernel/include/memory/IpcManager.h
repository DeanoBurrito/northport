#pragma once

#include <stddef.h>
#include <String.h>
#include <NativePtr.h>
#include <BufferView.h>
#include <Optional.h>
#include <IdAllocator.h>
#include <containers/Vector.h>

namespace Kernel::Memory
{
    enum class IpcStreamFlags
    {
        None = 0,
        UseSharedMemory = (1 << 0),
        KeepMailboxOpen = (1 << 1),
        SuppressUserAccess = (1 << 2),
    };

    enum class IpcAccessFlags : uint8_t
    {
        Disallowed = 0,
        Public = 1,
        SelectedOnly = 2,
        Private = 3,
    };
    
    struct IpcStream
    {
        IpcStreamFlags flags;
        sl::BufferView buffer;
        size_t ownerId;
        const sl::String name;

        IpcAccessFlags accessFlags;
        sl::Vector<size_t> accessList; // thread/process ids that can access this stream.

        IpcStream(const sl::String& name) : name(name) {}
    };
    
    class IpcManager
    {
    private:
        sl::UIdAllocator* idAlloc;
        //TODO: speed up checking for streams by having a hashmap [stream name -> stream index]
        sl::Vector<IpcStream*>* streams;
        char lock;

    public:
        static IpcManager* Global();

        void Init();

        sl::Opt<IpcStream*> StartStream(const sl::String& name, size_t length, IpcStreamFlags flags, IpcAccessFlags accessFlags);
        void StopStream(const sl::String& name);
        sl::Opt<sl::NativePtr> OpenStream(const sl::String& name, IpcStreamFlags flags);
        void CloseStream(const sl::String& name);
        //NOTE: this returns source details, and addresses will be relative to the owner's vmm.
        sl::Opt<IpcStream*> GetStreamDetails(const sl::String& name);

        sl::Opt<IpcStream*> CreateMailbox(const sl::String& mailbox, IpcStreamFlags flags, IpcAccessFlags accessFlags);
        void DestroyMailbox(const sl::String& mailbox);
        bool PostMail(const sl::String& destMailbox, const sl::BufferView data, bool keepOpenHint);
        bool MailAvailable(const sl::String& mailbox);
        sl::Opt<uint64_t> ReceiveMail(IpcStream* hostStream, sl::BufferView receiveInto);
    };
}
