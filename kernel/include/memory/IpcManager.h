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
        //user zero-copy method for streams: physical pages are mapped into both memory spaces.
        UseSharedMemory = (1 << 0),
        //a hint, can be ignored by the ipc subsystem. Indicates that a mailbox may multiple future operations pending.
        KeepMailboxOpen = (1 << 1),
        //used to open an ipc stream into an address space, but make it supervisor only.
        SuppressUserAccess = (1 << 2),
    };

    enum class IpcAccessFlags : uint8_t
    {
        //only the owner can access the ipc stream
        Disallowed = 0,
        //everyone can access the ipc stream
        Public = 1,
        //only processes/threads added to the accessList can use the stream.
        SelectedOnly = 2,
        //1-to-1 stream, the host and the first id of the accessList can access the stream.
        Private = 3,
    };

    struct IpcStream;
    struct IpcStreamClient;
    //first param is stream being referenced, second param is this client
    using StreamCleanupCallback = void (*)(const IpcStream* stream, const IpcStreamClient* client);
    
    struct IpcStreamClient
    {
        size_t threadGroupId;
        sl::BufferView localMapping;
        StreamCleanupCallback callback;

        IpcStreamClient(size_t id, sl::BufferView mapping, StreamCleanupCallback callback)
        : threadGroupId(id), localMapping(mapping), callback(callback)
        {}
    };
    
    struct IpcStream
    {
        IpcStreamFlags flags;
        sl::BufferView buffer;
        size_t ownerId;
        const sl::String name;

        IpcAccessFlags accessFlags;
        sl::Vector<size_t> accessList; // thread/threadgroup ids that can access this stream.
        sl::Vector<IpcStreamClient> clients;

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
        sl::Opt<sl::BufferView> OpenStream(const sl::String& name, IpcStreamFlags flags, const StreamCleanupCallback& callback);
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
