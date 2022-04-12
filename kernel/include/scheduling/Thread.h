#pragma once

#include <Platform.h>
#include <containers/Vector.h>
#include <memory/VirtualMemory.h>
#include <Optional.h>
#include <IdAllocator.h>
#include <scheduling/ThreadGroup.h>

namespace Kernel::Scheduling
{
    class Scheduler;

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

        //code is running in ring 0/supervisor mode
        KernelMode = (1 << 0),
        //code is currently running on a processor somewhere
        Executing = (1 << 1),
    };

    class Thread
    {
    friend Scheduler;
    private:
        char lock;
        ThreadFlags flags;
        ThreadState runState;
        size_t id;
        sl::Vector<size_t> waitReasons;
        ThreadGroup* parent;

        //there are actually where the stacks are
        sl::NativePtr programStack;
        sl::NativePtr kernelStack;
        //these are used for when we free this thread's stacks
        Memory::VMRange programStackRange;
        Memory::VMRange kernelStackRange;

        sl::String name;

        Thread() = default;
        
    public:
        static Thread* Current();

        FORCE_INLINE size_t Id() const
        { return id; }
        FORCE_INLINE ThreadFlags Flags() const
        { return flags; }
        FORCE_INLINE ThreadState State() const
        { return runState; }
        FORCE_INLINE ThreadGroup* Parent() const
        { return parent; }
        FORCE_INLINE sl::String& Name()
        { return name; }
        FORCE_INLINE const sl::String& Name() const
        { return name; }

        void Start(sl::NativePtr arg);
        void Exit();
        void Kill();
        void Sleep(size_t millis);
    };
}
