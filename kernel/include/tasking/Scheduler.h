#pragma once

#include <containers/Vector.h>
#include <containers/LinkedList.h>
#include <tasking/Thread.h>
#include <tasking/Dpc.h>
#include <Locks.h>
#include <Lazy.h>
#include <Random.h>

namespace Npk::Tasking
{
    using ThreadMain = void (*)(void* arg);

    enum class CoreState
    {
        PendingOnline,
        Available,
        PendingOffline,
    };

    struct SchedulerCore
    {
        Thread* queue; //this is an intrusive list, see the thread.next field.
        Thread* queueTail;
        Thread* idleThread;
        size_t threadCount;

        CoreState state = CoreState::PendingOnline;
        Npk::InterruptLock lock;

        sl::LinkedList<DeferredCall> dpcs;
        TrapFrame* dpcFrame;
        uintptr_t dpcStack;
        bool dpcFinished;
    };

    class Scheduler
    {
    private:
        sl::Vector<SchedulerCore*> cores;
        sl::Vector<Process*> processes;
        sl::Vector<Thread*> threadLookup;
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

    public:
        static Scheduler& Global();

        void Init();
        void RegisterCore();

        Process* CreateProcess();
        Thread* CreateThread(ThreadMain entry, void* arg, Process* parent = nullptr, size_t coreAffinity = -1);
        void DestroyThread(size_t id, size_t errorCode);
        void EnqueueThread(size_t id);
        //TODO: DequeueThread(), DestroyProcess()

        void QueueDpc(ThreadMain function, void* arg = nullptr);
        void DpcExit();

        void Reschedule();
        void SaveCurrentFrame(TrapFrame* current, RunLevel prevRunLevel);
        void RunNextFrame();
    };
}
