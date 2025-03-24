#pragma once

#include <hardware/Arch.h>
#include <core/Clock.h>
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
        bool active;

    public:
        ExecFrame* frame;
        ExtendedRegs* extFrame;
        sl::ListHook queueHook;
        WaitControl* waitControl; //for use in core/Event.cpp
    };

    using SchedulerQueue = sl::List<SchedulerObj, &SchedulerObj::queueHook>;

    class Scheduler
    {
    private:
        sl::RunLevelLock<RunLevel::Dpc> threadsLock;
        sl::Span<SchedulerQueue> liveThreads;
        SchedulerObj* idleThread;
        size_t coreId;

        DpcStore reschedDpc;
        ClockEvent reschedClockEvent;

        SchedulerObj* PopThread();
        void PushThread(SchedulerObj* obj);

    public:
        static Scheduler* Local();

        void Init(SchedulerObj* idle);
        [[noreturn]]
        void Kickstart();
        void Yield();

        size_t DefaultPriority() const;
        size_t MaxPriority() const;
        void Enqueue(SchedulerObj* obj, size_t priority);
        void Dequeue(SchedulerObj* obj);
        size_t GetPriority(SchedulerObj* obj);
        void SetPriority(SchedulerObj* obj, size_t newPriority);
        void AdjustPriority(SchedulerObj* obj, int adjustment);
    };

    SL_ALWAYS_INLINE
    size_t SchedPriorityMax()
    { return Scheduler::Local()->MaxPriority(); }

    SL_ALWAYS_INLINE
    size_t SchedPriorityDefault()
    { return Scheduler::Local()->DefaultPriority(); }

    SL_ALWAYS_INLINE
    void SchedYield()
    { return Scheduler::Local()->Yield(); }

    SL_ALWAYS_INLINE
    void SchedEnqueue(SchedulerObj* obj, size_t priority)
    { return Scheduler::Local()->Enqueue(obj, priority); }

    SL_ALWAYS_INLINE
    void SchedDequeue(SchedulerObj* obj)
    { return Scheduler::Local()->Dequeue(obj); }

    SL_ALWAYS_INLINE
    size_t SchedGetPriority(SchedulerObj* obj)
    { return Scheduler::Local()->GetPriority(obj); }

    SL_ALWAYS_INLINE
    void SchedSetPriority(SchedulerObj* obj, size_t newPriority)
    { return Scheduler::Local()->SetPriority(obj, newPriority); }

    SL_ALWAYS_INLINE
    void SchedAdjustPriority(SchedulerObj* obj, int adjustment)
    { return Scheduler::Local()->AdjustPriority(obj, adjustment); }
}
