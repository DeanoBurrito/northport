#pragma once

#include <arch/Interrupts.h>
#include <core/RunLevels.h>
#include <containers/List.h>
#include <Locks.h>
#include <Span.h>

namespace Npk::Core
{
    class Scheduler;
    struct WaitControl;

    struct SchedulerObj
    {
    friend Scheduler;
    private:
        sl::SpinLock lock;
        Scheduler* scheduler;
        size_t priority;
        TrapFrame* frame;
        void* extendedState;
        bool active;

    public:
        sl::ListHook queueHook;
        WaitControl* waitControl; //for use in core/Event.cpp
    };

    using SchedulerQueue = sl::List<SchedulerObj, &SchedulerObj::queueHook>;

    class Scheduler
    {
    private:
        sl::RunLevelLock<RunLevel::Dpc> threadsLock;
        sl::Span<SchedulerQueue> liveThreads;
        SchedulerObj idleThread;
        size_t coreId;

        SchedulerObj* PopThread();
        void PushThread(SchedulerObj* obj);

    public:
        static Scheduler* Local();

        void Init();
        [[noreturn]]
        void Kickstart();
        void Yield();

        size_t DefaultPriority() const;
        void Enqueue(SchedulerObj* obj, size_t priority);
        void Dequeue(SchedulerObj* obj);
        size_t GetPriority(SchedulerObj* obj);
        void SetPriority(SchedulerObj* obj, size_t newPriority);
        void AdjustPriority(SchedulerObj* obj, int adjustment);
    };

    ALWAYS_INLINE
    void SchedYield()
    { return Scheduler::Local()->Yield(); }

    ALWAYS_INLINE
    void SchedEnqueue(SchedulerObj* obj, size_t priority)
    { return Scheduler::Local()->Enqueue(obj, priority); }

    ALWAYS_INLINE
    void SchedDequeue(SchedulerObj* obj)
    { return Scheduler::Local()->Dequeue(obj); }

    ALWAYS_INLINE
    size_t SchedGetPriority(SchedulerObj* obj)
    { return Scheduler::Local()->GetPriority(obj); }

    ALWAYS_INLINE
    void SchedSetPriority(SchedulerObj* obj, size_t newPriority)
    { return Scheduler::Local()->SetPriority(obj, newPriority); }

    ALWAYS_INLINE
    void SchedAdjustPriority(SchedulerObj* obj, int adjustment)
    { return Scheduler::Local()->AdjustPriority(obj, adjustment); }
}
