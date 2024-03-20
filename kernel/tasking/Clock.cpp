#include <tasking/Clock.h>
#include <arch/Timers.h>
#include <debug/Log.h>
#include <interrupts/Ipi.h>
#include <containers/List.h>
#include <Locks.h>

namespace Npk::Tasking
{
    constexpr size_t TimekeepingEventNanos = 1'000'000;

    sl::SpinLock eventsLock;
    sl::IntrFwdList<ClockEvent> events;
    size_t eventsModifiedTicks;

    sl::Atomic<size_t> uptimeMillis;
    ClockEvent timekeepingEvent;
    DpcStore timekeepingDpc;

    static void UpdateTimerQueue()
    {
        const size_t pollTicks = PollTimer();
        size_t listDelta = PolledTicksToNanos(pollTicks - eventsModifiedTicks);
        eventsModifiedTicks = pollTicks;

        auto scan = events.Begin();
        while (scan != nullptr && listDelta > 0)
        {
            ASSERT_(scan != scan->next);
            if (scan->nanosRemaining == 0)
            {
                scan = scan->next;
                continue;
            }
            if (listDelta < scan->nanosRemaining)
            {
                scan->nanosRemaining -= listDelta;
                return;
            }

            listDelta -= scan->nanosRemaining;
            scan->nanosRemaining = 0;
            scan = scan->next;
        }
    }

    void ClockTickHandler()
    {
        ASSERT_(CoreLocal().runLevel >= RunLevel::Clock);

        UpdateTimerQueue();
        while (!events.Empty())
        {
            if (events.Front().nanosRemaining != 0)
                break;

            ClockEvent* event = events.PopFront();
            if (event->callbackCore == CoreLocal().id)
                QueueDpc(event->dpc);
            else
                QueueRemoteDpc(event->callbackCore, event->dpc);
        }
    }

    void ClockUptimeDpc(void*)
    {
        uptimeMillis.Add(TimekeepingEventNanos / 1'000'000, sl::Relaxed);

        timekeepingEvent.nanosRemaining = TimekeepingEventNanos;
        QueueClockEvent(&timekeepingEvent);
    }

    void StartSystemClock()
    {
        timekeepingDpc.data.function = ClockUptimeDpc;
        timekeepingEvent.callbackCore = CoreLocal().id;
        timekeepingEvent.nanosRemaining = TimekeepingEventNanos;
        timekeepingEvent.dpc = &timekeepingDpc;
        QueueClockEvent(&timekeepingEvent);

        Log("System clock started: intTimer=%s, pollTimer=%s", LogLevel::Info,
            SysTimerName(), PollTimerName());
    }

    static void RemoteQueueClockEvent(void* arg)
    { QueueClockEvent(static_cast<ClockEvent*>(arg)); }

    void QueueClockEvent(ClockEvent* event)
    {
        VALIDATE_(event != nullptr, );
        VALIDATE_(event->dpc != nullptr, );

        if (event->callbackCore == NoCoreAffinity)
            event->callbackCore = CoreLocal().id;
        if (CoreLocal().id != 0)
            return Interrupts::SendIpiMail(0, RemoteQueueClockEvent, event);

        const auto prevRl = EnsureRunLevel(RunLevel::Clock);
        eventsLock.Lock();

        UpdateTimerQueue();

        //special case: we're inserting at the front of the list
        if (events.Empty() || events.Front().nanosRemaining > event->nanosRemaining)
        {
            if (!events.Empty())
                events.Front().nanosRemaining -= event->nanosRemaining;
            events.PushFront(event);
            SetSysTimer(event->nanosRemaining, ClockTickHandler); //TODO: check we're not going beyond limits of timer

            eventsLock.Unlock();
            if (prevRl.HasValue())
                LowerRunLevel(*prevRl);
            return;
        }

        auto scan = events.Begin();
        bool inserted = false;
        while (scan != events.End())
        {
            event->nanosRemaining -= scan->nanosRemaining;
            if (scan->next == nullptr)
                break;
            if (scan->next->nanosRemaining < event->nanosRemaining)
                continue;

            events.InsertAfter(scan, event);
            event->next -= event->nanosRemaining;
            inserted = true;
            break;
        }

        if (!inserted)
            events.PushBack(event);

        eventsLock.Unlock();
        if (prevRl.HasValue())
            LowerRunLevel(*prevRl);
    }

    static void RemoteDequeueClockEvent(void* arg)
    { DequeueClockEvent(static_cast<ClockEvent*>(arg)); }

    void DequeueClockEvent(ClockEvent* event)
    {
        VALIDATE_(event != nullptr, );

        if (CoreLocal().id != 0)
            return Interrupts::SendIpiMail(0, RemoteDequeueClockEvent, event);

        const auto prevRl = EnsureRunLevel(RunLevel::Clock);
        eventsLock.Lock();
        events.Remove(event);
        eventsLock.Unlock();
        if (prevRl.HasValue())
            LowerRunLevel(*prevRl);
    }

    sl::ScaledTime GetUptime()
    { return { sl::TimeScale::Millis, uptimeMillis.Load(sl::Relaxed) }; }
}
