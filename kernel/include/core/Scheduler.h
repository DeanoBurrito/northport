#pragma once

#include <arch/Interrupts.h>
#include <core/RunLevels.h>
#include <containers/List.h>
#include <Locks.h>
#include <Span.h>

namespace Npk::Core
{
    class Scheduler;

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
    };

    using SchedulerQueue = sl::List<SchedulerObj, &SchedulerObj::queueHook>;

    class Scheduler
    {
    private:
        sl::RunLevelLock<RunLevel::Dpc> threadsLock;
        sl::Span<SchedulerQueue> liveThreads;
        SchedulerObj idleThread;

        SchedulerObj* PopThread();
        void PushThread(SchedulerObj* obj);

    public:
        static Scheduler* Local();

        void Init(sl::Span<uint8_t> idleStack);
        [[noreturn]]
        void Kickstart();

        size_t DefaultPriority() const;
        void Enqueue(SchedulerObj* obj, size_t priority);
        void Dequeue(SchedulerObj* obj);
        void Yield();
    };
}
