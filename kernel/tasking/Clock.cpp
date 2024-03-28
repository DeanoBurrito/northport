#include <tasking/Clock.h>
#include <arch/Timers.h>
#include <debug/Log.h>
#include <interrupts/Ipi.h>
#include <containers/List.h>
#include <Locks.h>
#include <Maths.h>

namespace Npk::Tasking
{
    constexpr sl::ScaledTime TimekeepingEventDuration = 5_ms;

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
            ASSERT(scan != scan->next, "Tried to queue the same event twice?");
            if (scan->duration.units == 0)
            {
                scan = scan->next;
                continue;
            }
            if (listDelta < scan->duration.units)
            {
                scan->duration.units -= listDelta;
                return;
            }

            listDelta -= scan->duration.units;
            scan->duration.units = 0;
            scan = scan->next;
        }
    }

    void ClockTickHandler()
    {
        ASSERT_(CoreLocal().runLevel >= RunLevel::Clock);

        UpdateTimerQueue();
        while (!events.Empty())
        {
            if (events.Front().duration.units != 0)
                break;

            ClockEvent* event = events.PopFront();
            if (event->callbackCore == CoreLocal().id)
                QueueDpc(event->dpc);
            else
                QueueRemoteDpc(event->callbackCore, event->dpc);
        }

        if (!events.Empty())
        {
            const size_t clockArmTime = sl::Min(SysTimerMaxNanos(), events.Front().duration.units);
            SetSysTimer(clockArmTime, ClockTickHandler);
        }
    }

    void ClockUptimeDpc(void*)
    {
        uptimeMillis.Add(TimekeepingEventDuration.ToMillis(), sl::Relaxed);

        timekeepingEvent.duration = TimekeepingEventDuration;
        QueueClockEvent(&timekeepingEvent);
    }

    void StartSystemClock()
    {
        timekeepingDpc.data.function = ClockUptimeDpc;
        timekeepingEvent.callbackCore = CoreLocal().id;
        timekeepingEvent.dpc = &timekeepingDpc;
        timekeepingEvent.duration = TimekeepingEventDuration;
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
        event->duration = event->duration.ToScale(sl::TimeScale::Nanos);

        //special case: we're inserting at the front of the list
        if (events.Empty() || events.Front().duration.units > event->duration.units)
        {
            if (!events.Empty())
                events.Front().duration.units -= event->duration.units;
            events.PushFront(event);
            const size_t clockArmTime = sl::Min(SysTimerMaxNanos(), event->duration.units);
            SetSysTimer(clockArmTime, ClockTickHandler);

            eventsLock.Unlock();
            if (prevRl.HasValue())
                LowerRunLevel(*prevRl);
            return;
        }

        auto scan = events.Begin();
        bool inserted = false;
        while (scan != events.End())
        {
            event->duration.units -= scan->duration.units;
            if (scan->next == nullptr)
                break;
            if (scan->next->duration.units < event->duration.units)
            {
                scan = scan->next;
                continue;
            }

            events.InsertAfter(scan, event);
            if (event->next != nullptr)
                event->next -= event->duration.units;
            inserted = true;
            break;
        }

        if (!inserted)
            events.PushBack(event);

        eventsLock.Unlock();
        if (prevRl.HasValue())
            LowerRunLevel(*prevRl);
    }

    void DequeueClockEvent(ClockEvent* event)
    {
        VALIDATE_(event != nullptr, );

        const auto prevRl = EnsureRunLevel(RunLevel::Clock);
        eventsLock.Lock();
        ClockEvent* following = events.Remove(event);
        if (following != nullptr)
            following->duration.units += event->duration.units;
        eventsLock.Unlock();
        if (prevRl.HasValue())
            LowerRunLevel(*prevRl);
    }

    sl::ScaledTime GetUptime()
    { return { sl::TimeScale::Millis, uptimeMillis.Load(sl::Relaxed) }; }
}
