#pragma once

#include <Platform.h>
#include <containers/Vector.h>
#include <memory/VirtualMemory.h>
#include <Optional.h>

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

        sl::NativePtr programStack;
        sl::NativePtr kernelStack;

        ThreadGroup* parent;

        Thread() = default;
        
    public:
        static Thread* Current();

        size_t GetId() const;
        ThreadFlags GetFlags() const;
        ThreadState GetState() const;
        ThreadGroup* GetParent() const;

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

        ThreadGroup() = default;

    public:
        const sl::Vector<Thread*>& Threads() const;
        const Thread* ParentThread() const;
        size_t Id() const;
        Memory::VirtualMemoryManager* VMM();
    };
}
