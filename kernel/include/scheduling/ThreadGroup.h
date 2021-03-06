#pragma once

#include <IdAllocator.h>
#include <memory/VirtualMemory.h>
#include <scheduling/ThreadGroupEvent.h>
#include <Bitmap.h>
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
        sl::Bitmap userStackBitmap;
        sl::Bitmap kernelStackBitmap;
        
        sl::UIdAllocator resourceIdAlloc;
        sl::Vector<ThreadResource> resources;
        sl::Vector<ThreadGroupEvent> events;

        ThreadGroup() = default;

        sl::NativePtr AllocUserStack();
        void FreeUserStack(sl::NativePtr base);
        sl::NativePtr AllocKernelStack();
        void FreeKernelStack(sl::NativePtr base);

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
        bool DetachResource(size_t rid);
        sl::Opt<ThreadResource*> GetResource(size_t rid);
        sl::Opt<size_t> FindResource(sl::NativePtr data);
        
        size_t PendingEventCount() const;
        void PushEvent(const ThreadGroupEvent& event);
        void PushEvents(const sl::Vector<ThreadGroupEvent>& eventList);
        sl::Opt<ThreadGroupEvent> PeekEvent();
        sl::Opt<ThreadGroupEvent> ConsumeEvent();
    };
}
