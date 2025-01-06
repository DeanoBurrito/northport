#include <core/Event.h>
#include <core/Scheduler.h>
#include <core/Log.h>
#include <core/Clock.h>

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

    WaitResult WaitManager::WaitOne()
    {
        ASSERT_UNREACHABLE();
    }

    static void HandleWaitTimeout(void* arg)
    {
        WaitControl* ctrl = static_cast<WaitControl*>(arg);
        VALIDATE_(ctrl != nullptr, );
        Log("Wait timed out", LogLevel::Debug);

        ctrl->timedOut = true;
        SchedEnqueue(ctrl->thread, ctrl->threadPriority);
    }

    WaitResult WaitManager::WaitMany(sl::Span<Waitable*> events, WaitEntry* entries, sl::ScaledTime timeout, bool waitAll)
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

        //init wait entries
        for (size_t i = 0; i < events.Size(); i++)
        {
            entries[i].control = &control;
            entries[i].entryIndex = i;
            entries[i].satisfied = false;
        }

        //if there is a timeout (0 == poll once, -1 == wait indefinitely) queue the clock event
        if (timeout.units != 0 && timeout.units != -1ull)
            QueueClockEvent(&control.timeoutEvent);

        while (true)
        {
            ASSERT_(LockAll(events)); //TODO: handle by sleeping instead of asserting lol

            //try satisfy the wait immediately
            if (auto result = TryFinish(control, waitAll); result.HasValue() || timeout.units == 0)
            {
                UnlockAll(events); //TODO: sync with timeout event here, since its memory is freed when we return
                return result.HasValue() ? *result : WaitResult::Timeout;
            }

            //add a wait entry into the queue for each event we're waiting on
            for (size_t i = 0; i < events.Size(); i++)
                events[i]->waiters.PushBack(&entries[i]);
            SchedDequeue(control.thread);
            UnlockAll(events);
            SchedYield();
        }
    }

    void WaitManager::CancelWait(SchedulerObj* thread)
    {}
}
