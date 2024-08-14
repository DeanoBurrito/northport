#include <tasking/Clock.h>
#include <arch/Timers.h>
#include <debug/Log.h>
#include <interrupts/Ipi.h>
#include <Maths.h>

namespace Npk::Tasking
{
    constexpr sl::ScaledTime TimekeepingEventDuration = 5_ms;

    size_t globalQueueOwnerCore;
    ClockQueue globalQueue;

    sl::Atomic<size_t> uptimeMillis;
    ClockEvent timekeepingEvent;
    DpcStore timekeepingDpc;

    static void UpdateTimerQueue()
    {
        const size_t pollTicks = PollTimer();
        size_t listDelta = PollTicksToNanos(pollTicks - globalQueue.modifiedTicks);
        globalQueue.modifiedTicks = pollTicks;

        auto scan = globalQueue.events.Begin();
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

    bool ClockTickHandler(void*)
    {
        ASSERT_(CoreLocal().runLevel >= RunLevel::Clock);

        UpdateTimerQueue();
        while (!globalQueue.events.Empty())
        {
            if (globalQueue.events.Front().duration.units != 0)
                break;

            ClockEvent* event = globalQueue.events.PopFront();
            if (event->callbackCore == CoreLocal().id)
                QueueDpc(event->dpc);
            else
                QueueRemoteDpc(event->callbackCore, event->dpc);
        }

        if (!globalQueue.events.Empty())
        {
            const size_t clockArmTime = sl::Min(InterruptTimerMaxNanos(), globalQueue.events.Front().duration.units);
            ArmInterruptTimer(clockArmTime, ClockTickHandler);
        }

        //return value isnt super important here, it just allows *not* enqueueing a dpc associated with the interrupt
        //route, which isnt present anyway. Its required as part of the function signature for an interupt route endpoint.
        return true;
    }

    void ClockUptimeDpc(void*)
    {
        uptimeMillis.Add(TimekeepingEventDuration.ToMillis(), sl::Relaxed);

        timekeepingEvent.duration = TimekeepingEventDuration;
        QueueClockEvent(&timekeepingEvent);
    }

    void StartSystemClock()
    {
        globalQueueOwnerCore = 0; //TODO: per-cpu queues

        timekeepingDpc.data.function = ClockUptimeDpc;
        timekeepingEvent.callbackCore = CoreLocal().id;
        timekeepingEvent.dpc = &timekeepingDpc;
        timekeepingEvent.duration = TimekeepingEventDuration;
        QueueClockEvent(&timekeepingEvent);

        Log("System clock started: intTimer=%s, pollTimer=%s", LogLevel::Info,
            InterruptTimerName(), PollTimerName());
    }

    static void RemoteQueueClockEvent(void* arg)
    { QueueClockEvent(static_cast<ClockEvent*>(arg)); }

    void QueueClockEvent(ClockEvent* event)
    {
        VALIDATE_(event != nullptr, );
        VALIDATE_(event->dpc != nullptr, );

        if (event->callbackCore == NoCoreAffinity)
            event->callbackCore = CoreLocal().id;

        if (globalQueueOwnerCore != CoreLocal().id)
            return Interrupts::SendIpiMail(globalQueueOwnerCore, RemoteQueueClockEvent, event);

        sl::ScopedLock scopeLock(globalQueue.lock);

        UpdateTimerQueue();
        event->duration = event->duration.ToScale(sl::TimeScale::Nanos);

        //special case: we're inserting at the front of the list
        if (globalQueue.events.Empty() || globalQueue.events.Front().duration.units > event->duration.units)
        {
            if (!globalQueue.events.Empty())
                globalQueue.events.Front().duration.units -= event->duration.units;
            globalQueue.events.PushFront(event);
            const size_t clockArmTime = sl::Min(InterruptTimerMaxNanos(), event->duration.units);
            ArmInterruptTimer(clockArmTime, ClockTickHandler);

            return;
        }

        auto scan = globalQueue.events.Begin();
        bool inserted = false;
        while (scan != globalQueue.events.End())
        {
            event->duration.units -= scan->duration.units;
            if (scan->next == nullptr)
                break;
            if (scan->next->duration.units < event->duration.units)
            {
                scan = scan->next;
                continue;
            }

            globalQueue.events.InsertAfter(scan, event);
            if (event->next != nullptr)
                event->next -= event->duration.units;
            inserted = true;
            break;
        }

        if (!inserted)
            globalQueue.events.PushBack(event);
    }

    static void RemoteDequeueClockEvent(void* arg)
    { DequeueClockEvent(static_cast<ClockEvent*>(arg)); }

    void DequeueClockEvent(ClockEvent* event)
    {
        VALIDATE_(event != nullptr, );

        if (globalQueueOwnerCore != CoreLocal().id)
            return Interrupts::SendIpiMail(globalQueueOwnerCore, RemoteDequeueClockEvent, event);

        sl::ScopedLock scopeLock(globalQueue.lock);
        UpdateTimerQueue();

        if (globalQueue.events.Begin() == event)
        {
            globalQueue.events.PopFront();
            if (globalQueue.events.Empty())
                return;

            auto& front = globalQueue.events.Front();
            front.duration.units -= event->duration.units;
            const size_t clockArmTime = sl::Min(InterruptTimerMaxNanos(), front.duration.units);
            ArmInterruptTimer(clockArmTime, ClockTickHandler);
            return;
        }

        ClockEvent* following = globalQueue.events.Remove(event);
        if (following != nullptr)
            following->duration.units += event->duration.units;
    }

    sl::ScaledTime GetUptime()
    { return { sl::TimeScale::Millis, uptimeMillis.Load(sl::Relaxed) }; }
}
