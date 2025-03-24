#include <core/Event.h>
#include <core/Scheduler.h>
#include <core/Log.h>
#include <core/Clock.h>
#include <Maths.h>

namespace Npk::Core
{
    struct WaitControl
    {
        SchedulerObj* thread;
        size_t threadPriority;
        DpcStore timeoutDpc;
        ClockEvent timeoutEvent;

        sl::Span<Waitable*> events;
        WaitEntry* entries;
        sl::Atomic<bool> timedOut;
        sl::Atomic<bool> cancelled;
    };

    void Waitable::Signal(size_t amount)
    {
        sl::ScopedLock scopeLock(lock);
        count = sl::Clamp(count + amount, (size_t)0, maxCount);

        if (waiters.Empty())
            return;

        //TODO: change this to SignalCount() and add SignalAll
        auto control = waiters.Front().control;
        //TODO: instead of enqueuing the thread, we should try complete the wait
        //for them ('help' them), if the first thread cant be completed, try the next one?
        SchedEnqueue(control->thread, control->threadPriority); //TODO: apply temporary priority boost here
    }

    void Waitable::Reset(size_t initialCount, size_t newMaxCount)
    {
        sl::ScopedLock scopeLock(lock);

        count = initialCount;
        maxCount = newMaxCount;

        if (count == 0)
            return;

        scopeLock.Release();
        Signal(0); //signal any already waiting threads
    }

    bool WaitManager::LockAll(sl::Span<Waitable*> events)
    {
        for (size_t i = 0; i < events.Size(); i++)
            events[i]->lock.Lock();
        return true;
    }

    void WaitManager::UnlockAll(sl::Span<Waitable*> events)
    {
        for (size_t i = 0; i < events.Size(); i++)
            events[i]->lock.Unlock();
    }

    sl::Opt<WaitResult> WaitManager::TryFinish(WaitControl& control, bool waitAll)
    {
        if (control.timedOut)
            return WaitResult::Timeout;
        if (control.cancelled)
            return WaitResult::Cancelled;

        size_t completedCount = 0;
        for (size_t i = 0; i < control.events.Size(); i++)
            completedCount += control.events[i]->count != 0 ? 1 : 0;

        if (!waitAll)
        {
            if (completedCount == 0)
                return {};

            for (size_t i = 0; i < control.events.Size(); i++)
            {
                if (control.events[i]->count == 0)
                    continue;
                control.events[i]->count--;
                control.entries[i].satisfied = true;
            }
            return WaitResult::Success;
        }

        if (completedCount != control.events.Size())
            return {};
        for (size_t i = 0; i < control.events.Size(); i++)
        {
            control.events[i]->count--;
            control.entries[i].satisfied = true;
        }
        return WaitResult::Success;
    }

    WaitResult WaitManager::WaitOne(Waitable* event, WaitEntry* entry, sl::TimeCount timeout)
    {
        const size_t eventCount = event == nullptr ? 0 : 1;
        return WaitMany({ &event, eventCount }, entry, timeout, false);
    }

    static void HandleWaitTimeout(void* arg)
    {
        WaitControl* ctrl = static_cast<WaitControl*>(arg);
        VALIDATE_(ctrl != nullptr, );

        ctrl->timedOut = true;
        SchedEnqueue(ctrl->thread, ctrl->threadPriority);
    }

    WaitResult WaitManager::WaitMany(sl::Span<Waitable*> events, WaitEntry* entries, sl::TimeCount timeout, bool waitAll)
    {
        //control block for wait operation
        WaitControl control {};
        control.thread = static_cast<SchedulerObj*>(GetLocalPtr(SubsysPtr::Thread));
        control.threadPriority = SchedGetPriority(control.thread);
        control.entries = entries;
        control.events = events;
        control.timedOut = false;
        control.cancelled = false;
        control.timeoutDpc.data.arg = &control;
        control.timeoutDpc.data.function = HandleWaitTimeout;
        control.timeoutEvent.dpc = &control.timeoutDpc;
        control.timeoutEvent.expiry = timeout;

        control.thread->waitControl = &control;

        //init wait entries
        for (size_t i = 0; i < events.Size(); i++)
        {
            entries[i].control = &control;
            entries[i].satisfied = false;
        }

        //only queue the clock event if there's a timeout and we're not polling (timeout.ticks == 0)
        bool hasTimeout = false;
        if (timeout.ticks != 0 && timeout != NoTimeout)
        {
            hasTimeout = true;
            QueueClockEvent(&control.timeoutEvent);
        }

        bool inQueues = false;
        while (true)
        {
            ASSERT_(LockAll(events)); //TODO: handle by sleeping instead of asserting lol
            
            if (inQueues)
            {
                for (size_t i = 0; i < events.Size(); i++)
                    events[i]->waiters.Remove(&entries[i]);
                inQueues = false;
            }

            //try satisfy the wait immediately
            if (auto result = TryFinish(control, waitAll); result.HasValue() || timeout.ticks == 0)
            {
                UnlockAll(events);

                //sycnhronize with timeout event
                if (hasTimeout && !control.timedOut)
                {
                    if (!DequeueClockEvent(&control.timeoutEvent))
                    {
                        while (!control.timedOut)
                            sl::HintSpinloop();
                    }
                }

                control.thread->waitControl = nullptr;
                return result.HasValue() ? *result : WaitResult::Timeout;
            }

            //add a wait entry into the queue for each event we're waiting on
            for (size_t i = 0; i < events.Size(); i++)
                events[i]->waiters.PushBack(&entries[i]);
            inQueues = true;

            SchedDequeue(control.thread);
            UnlockAll(events);
            SchedYield();
        }
    }

    void WaitManager::CancelWait(SchedulerObj* thread)
    {}
}
