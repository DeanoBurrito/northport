#pragma once

#include <containers/Vector.h>
#include <containers/LinkedList.h>
#include <memory/Heap.h>
#include <tasking/Thread.h>
#include <tasking/Dpc.h>
#include <Locks.h>
#include <Lazy.h>
#include <Random.h>
#include <Optional.h>

namespace Npk::Tasking
{
    constexpr size_t NoAffinity = -1ul;
    
    using ThreadMain = void (*)(void* arg);

    struct SchedulerCore
    {
        Thread* queue; //this is an intrusive list, see the thread.next field.
        Thread* queueTail;
        Thread* idleThread;
        sl::Atomic<size_t> threadCount;

        size_t coreId;
        Npk::InterruptLock lock;
        sl::Atomic<bool> suspendScheduling;

        sl::LinkedList<DeferredCall, Memory::CachingSlab<16>> dpcs;
        TrapFrame* dpcFrame;
        uintptr_t dpcStack;
        bool dpcFinished;
    };

    class ScheduleGuard;

    class Scheduler
    {
    friend ScheduleGuard;
    private:
        sl::LinkedList<SchedulerCore*> cores;
        sl::Vector<Process*> processes;
        sl::Vector<Thread*> threadLookup;
        sl::TicketLock coresListLock; //only for modifying the list, not the items within.
        sl::TicketLock threadsLock;
        sl::TicketLock processesLock;
        
        size_t nextTid; //TODO: id allocators for these
        size_t nextPid;
        Process* idleProcess;
        size_t activeCores = 0;

        sl::TicketLock rngLock;
        sl::Lazy<sl::XoshiroRng> rng;

        void LateInit();
        size_t NextRand();

        SchedulerCore* GetCore(size_t id);

    public:
        static Scheduler& Global();

        void Init();
        void RegisterCore(Thread* initThread = nullptr);
        sl::Opt<Thread*> GetThread(size_t id);
        sl::Opt<Process*> GetProcess(size_t id);

        void QueueDpc(ThreadMain function, void* arg = nullptr);
        void DpcExit();

        Process* CreateProcess();
        Thread* CreateThread(ThreadMain entry, void* arg, Process* parent = nullptr, size_t coreAffinity = NoAffinity);
        void DestroyProcess(size_t id);
        void DestroyThread(size_t id, size_t errorCode);
        void EnqueueThread(size_t id);
        void DequeueThread(size_t id);

        void Yield(bool willReturn = true);
        void Reschedule();
        bool Suspend(bool yes);

        void SaveCurrentFrame(TrapFrame* current, RunLevel prevRunLevel);
        void RunNextFrame();
    };

    class ScheduleGuard
    {
    private:
        bool prevState;

    public:
        ScheduleGuard()
        {
            prevState = Scheduler::Global().Suspend(true);
        }

        ~ScheduleGuard()
        {
            Scheduler::Global().Suspend(prevState);
        }

        ScheduleGuard(const ScheduleGuard& other) = delete;
        ScheduleGuard& operator=(const ScheduleGuard& other) = delete;
        ScheduleGuard(ScheduleGuard&& other) = delete;
        ScheduleGuard& operator=(ScheduleGuard&& other) = delete;
    };
}
