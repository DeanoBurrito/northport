#pragma once

#include <IdAllocator.h>
#include <scheduling/Thread.h>
#include <containers/Vector.h>
#include <containers/LinkedList.h>

#define SCHEDULER_TIMER_TICK_MS 10

namespace Kernel::Scheduling
{
    class Scheduler
    {
    friend Thread;
    private:
        char lock;
        bool suspended;
        size_t realPressure;
        sl::UIdAllocator idAllocator;
        sl::Vector<Thread*> allThreads;
        sl::Vector<Thread*> idleThreads;
        Thread* lastSelectedThread;

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

        Thread* CreateThread(sl::NativePtr entryAddress, ThreadFlags flags, ThreadGroup* parent = nullptr);
        void RemoveThread(size_t id);
        Thread* GetThread(size_t id) const;
    };
}
