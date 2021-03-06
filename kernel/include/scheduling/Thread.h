#pragma once

#include <Platform.h>
#include <scheduling/ThreadGroup.h>

namespace Kernel::Scheduling
{
    class Scheduler;
    class ThreadGroup;

    enum class ThreadState
    {
        PendingStart,
        PendingCleanup,
        Running,
        Sleeping,
        SleepingForEvents,
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
    friend ThreadGroup;
    private:
        char lock;
        ThreadFlags flags;
        ThreadState runState;
        size_t id;
        size_t core;
        ThreadGroup* parent;

        sl::NativePtr programStack;
        sl::NativePtr kernelStack;

        sl::String name;
        size_t wakeTime;

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
        //NOTE: Exit() can be used on non-local threads, so there's no Kill().
        void Sleep(size_t millis);
        void SleepUntilEvent(size_t timeout); //timeout of 0 means indefinite
    };
}
