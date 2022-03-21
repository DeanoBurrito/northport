#pragma once

#include <IdAllocator.h>
#include <scheduling/Thread.h>
#include <scheduling/BuiltinThreads.h>
#include <containers/Vector.h>
#include <containers/LinkedList.h>

#define SCHEDULER_TIMER_TICK_MS 10

namespace Kernel::Scheduling
{
    //holds details about what a particular core is up to
    struct SchedulerProcessorStatus
    {
        bool isIdling = false;
        Thread* currentThread = nullptr;
        Thread* idleThread = nullptr;

        //there may be gaps in how we store these (if cpu-ids have gaps in them).
        [[gnu::always_inline]] inline
        bool IsPlaceholder()
        { return currentThread == nullptr && idleThread == nullptr; }
    };

    class Scheduler
    {
    friend Thread;
    private:
        char lock;
        bool suspended;
        size_t realPressure;
        sl::UIdAllocator idAllocator;
        sl::Vector<Thread*> allThreads;
        sl::Vector<SchedulerProcessorStatus> processorStatus;
        Thread* lastSelectedThread;

        //anything contained here is available to the cleanup thread
        CleanupData cleanupData;

        void SaveContext(Thread* thread, StoredRegisters* regs);
        StoredRegisters* LoadContext(Thread* thread);

    public:
        static Scheduler* Global();

        void Init(size_t coreCount);
        StoredRegisters* Tick(StoredRegisters* current);
        [[noreturn]]
        void Yield();
        void Suspend(bool suspendScheduling = true);

        size_t GetPressure() const;

        ThreadGroup* CreateThreadGroup();
        Thread* CreateThread(sl::NativePtr entryAddress, ThreadFlags flags, ThreadGroup* parent = nullptr);
        void RemoveThread(size_t id);
        Thread* GetThread(size_t id) const;
    };
}
