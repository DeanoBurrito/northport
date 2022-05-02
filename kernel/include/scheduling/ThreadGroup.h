#pragma once

#include <IdAllocator.h>
#include <memory/VirtualMemory.h>
#include <scheduling/ThreadGroupEvent.h>
#include <String.h>

namespace Kernel::Scheduling
{
    class Scheduler;
    class Thread;

    enum class ThreadResourceType
    {
        Empty, //represents an empty slot

        FileHandle,
        IpcStream,
        IpcMailbox,
    };
    
    struct ThreadResource
    {
        ThreadResourceType type;
        sl::NativePtr res;

        ThreadResource(ThreadResourceType t, sl::NativePtr r) : type(t), res(r) {}
    };
    
    class ThreadGroup
    {
    friend Scheduler;
    private:
        char lock;
        size_t id;
        Thread* parent;
        sl::String name;
        sl::Vector<Thread*> threads;
        Memory::VirtualMemoryManager vmm;
        
        sl::UIdAllocator resourceIdAlloc;
        sl::Vector<ThreadResource> resources;
        sl::Vector<ThreadGroupEvent> events;

        ThreadGroup() = default;

    public:
        static ThreadGroup* Current();

        FORCE_INLINE const sl::Vector<Thread*>& Threads() const
        { return threads; }
        FORCE_INLINE const Thread* ParentThread() const
        { return parent; }
        FORCE_INLINE size_t Id() const
        { return id; }
        FORCE_INLINE Memory::VirtualMemoryManager* VMM()
        { return &vmm; }
        FORCE_INLINE sl::String& Name()
        { return name; }

        sl::Opt<size_t> AttachResource(ThreadResourceType type, sl::NativePtr resource);
        bool DetachResource(size_t rid, bool force = false);
        sl::Opt<ThreadResource*> GetResource(size_t rid);
        
        size_t PendingEventCount() const;
        void PushEvent(const ThreadGroupEvent& event);
        sl::Opt<ThreadGroupEvent> PeekEvent();
        sl::Opt<ThreadGroupEvent> ConsumeEvent();
    };
}
