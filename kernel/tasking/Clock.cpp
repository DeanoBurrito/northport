#include <tasking/Clock.h>
#include <arch/Platform.h>
#include <debug/Log.h>
#include <containers/Vector.h>
#include <Locks.h>

namespace Npk::Tasking
{
    struct ClockEvent
    {
        size_t nanosRemaining;
        void* data;
        void (*Callback)(void*);
        size_t period;

        ClockEvent() = default;
        ClockEvent(size_t nanos, void* data, void (*cb)(void*), size_t period)
        : nanosRemaining(nanos), data(data), Callback(cb), period(period)
        {}
    };

    volatile size_t uptimeMillis = 0;
    sl::TicketLock eventsLock;
    sl::Vector<ClockEvent> events; //TODO: implement sl::Queue<T>, use it here

    void ClockEventDispatch(size_t)
    {
        //TODO: we dont account for the time taken by the callback to run.
        //future timer events could expire while these are running, we'll need some sort of polling clock too?
        const ClockEvent& event = events.Front();
        if (event.Callback != nullptr)
            event.Callback(event.data);
        
        if (event.period != 0)
            QueueClockEvent(event.period, event.data, event.Callback, true);
        
        events.Erase(0);
        ASSERT(events.Size() > 0, "System clock has empty event queue. No time-keeping event?");

        SetSystemTimer(events[0].nanosRemaining, nullptr);
    }

    void UptimeEventCallback(void*)
    {
        //TODO: investigate, should I be using __atomic_add/__atomic_load instead of volatile?
        ++uptimeMillis;
    }

    void StartSystemClock()
    {
        QueueClockEvent(1'000'000, nullptr, UptimeEventCallback, true); //time-keeping event
        SetSystemTimer(events[0].nanosRemaining, ClockEventDispatch);

        Log("System clock start: uptimeTick=1ms", LogLevel::Info);
    }

    void QueueClockEvent(size_t nanoseconds, void* payloadData, void (*callback)(void* data), bool periodic)
    {
        InterruptGuard guard;
        sl::ScopedLock scopeLock(eventsLock);

        for (size_t i = 0; i < events.Size(); i++)
        {
            while (events[i].nanosRemaining < nanoseconds)
            {
                nanoseconds -= events[i].nanosRemaining;
                continue;
            }

            events.Emplace(i, nanoseconds, payloadData, callback, periodic ? nanoseconds : 0);
            return;
        }

        events.EmplaceBack(nanoseconds, payloadData, callback, periodic ? nanoseconds : 0);
    }

    size_t GetUptime()
    {
        return uptimeMillis;
    }
}
