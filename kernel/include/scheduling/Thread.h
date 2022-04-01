#pragma once

#include <Platform.h>
#include <containers/Vector.h>
#include <memory/VirtualMemory.h>
#include <Optional.h>
#include <IdAllocator.h>

namespace Kernel::Scheduling
{
    enum class ThreadState
    {
        PendingStart,
        PendingCleanup,
        Running,
        Waiting,
    };

    enum class ThreadFlags
    {
        None = 0,

        //code is running in ring 0
        KernelMode = (1 << 0),
        //code is currently running on a processor somewhere
        Executing = (1 << 1),
    };

    enum class ThreadResourceType
    {
        Empty, //represents an empty slot

        FileHandle,
        IpcStream,
    };

    struct ThreadResource
    {
        ThreadResourceType type;
        sl::NativePtr res;

        ThreadResource(ThreadResourceType t, sl::NativePtr r) : type(t), res(r) {}
    };

    class Scheduler;
    class ThreadGroup;

    class Thread
    {
    friend Scheduler;
    private:
        char lock;
        ThreadFlags flags;
        ThreadState runState;
        size_t id;
        sl::Vector<size_t> waitJobs;

        //there are actually where the stacks are
        sl::NativePtr programStack;
        sl::NativePtr kernelStack;
        //these are used for when we free this thread's stacks
        Memory::VMRange programStackRange;
        Memory::VMRange kernelStackRange;

        ThreadGroup* parent;

        Thread() = default;
        
    public:
        static Thread* Current();

        size_t Id() const;
        ThreadFlags Flags() const;
        ThreadState State() const;
        ThreadGroup* Parent() const;

        void Start(sl::NativePtr arg);
        void Exit();
        void Kill();
        void Sleep(size_t millis);
    };

    class ThreadGroup
    {
    friend Scheduler;
    private:
        char lock;
        size_t id;
        Thread* parent;
        sl::Vector<Thread*> threads;
        Memory::VirtualMemoryManager vmm;
        //handles and group events (most of them) stored here
        sl::UIdAllocator resourceIdAlloc;
        sl::Vector<ThreadResource> resources;

        ThreadGroup() = default;

    public:
        static ThreadGroup* Current();

        const sl::Vector<Thread*>& Threads() const;
        const Thread* ParentThread() const;
        size_t Id() const;
        Memory::VirtualMemoryManager* VMM();

        sl::Opt<size_t> AttachResource(ThreadResourceType type, sl::NativePtr resource);
        bool DetachResource(size_t rid, bool force = false);
        sl::Opt<ThreadResource*> GetResource(size_t rid);
    };
}
