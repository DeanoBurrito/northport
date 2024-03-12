#pragma once

#include <tasking/Threads.h>
#include <Locks.h>
#include <containers/List.h>

namespace Npk::Tasking
{
    constexpr size_t EnginesPerCluster = 4;

    struct WorkQueue
    {
        //TODO: priority queues
        sl::Atomic<size_t> depth;
        sl::SpinLock lock;
        sl::IntrFwdList<Thread> threads;
    };

    enum EngineFlag
    {
        Clutch, //engine will not take control away from the current thread while this is set
        ReschedulePending, //engine will reschedule when next possible
    };

    using EngineFlags = sl::Flags<EngineFlag>;

    struct EngineCluster;

    //represents a logical processor within the system
    struct Engine
    {
        size_t id;
        EngineFlags flags;
        WorkQueue localQueue;
        size_t localTickets;
        size_t sharedTickets;

        EngineCluster* cluster;
        Thread* idleThread;
        size_t extRegsOwner;

        DpcStore rescheduleDpc;
    };

    //a small group of processors that share a work queue, for easy load-balancing
    struct EngineCluster
    {
        EngineCluster* next;

        WorkQueue sharedQueue;
        size_t engineCount;
        Engine engines[EnginesPerCluster];
    };

    class Scheduler
    {
    private:
        sl::RwLock enginesLock;
        sl::Vector<Engine*> engines;
        sl::IntrFwdList<EngineCluster> clusters;

        static void DoReschedule(void* arg);
        static void RemoteDequeue(void* arg);

        void LateInit();

    public:
        static Scheduler& Global();

        void Init();
        void AddEngine();
        [[noreturn]]
        void StartEngine();
        //TODO: StopEngine() and RemoveEngine()
        
        bool Suspend(bool yes);
        void Yield();

        void EnqueueThread(Thread* t);
        void DequeueThread(Thread* t);
        void QueueReschedule();
        void SwapExtendedRegs();
    };

    struct ScheduleGuard
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

        ScheduleGuard(const ScheduleGuard&) = delete;
        ScheduleGuard& operator=(const ScheduleGuard&) = delete;
        ScheduleGuard(ScheduleGuard&&) = delete;
        ScheduleGuard& operator=(ScheduleGuard&&) = delete;
    };
}
