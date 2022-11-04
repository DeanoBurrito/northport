#pragma once

#include <containers/Vector.h>
#include <containers/LinkedList.h>
#include <tasking/Thread.h>
#include <tasking/Dpc.h>
#include <Locks.h>

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
        sl::Vector<Thread*> threads;
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

    public:
        static Scheduler& Global();

        void Init();
        void RegisterCore(bool beginScheduling);

        Process* CreateProcess(void* environment);
        Thread* CreateThread(ThreadMain entry, void* arg, Process* parent = nullptr, size_t coreAffinity = -1);
        void QueueDpc(ThreadMain function, void* arg = nullptr);
        void DpcExit();

        void Reschedule();
        void SaveCurrentFrame(TrapFrame* current, RunLevel prevRunLevel);
        void RunNextFrame();
    };
}
