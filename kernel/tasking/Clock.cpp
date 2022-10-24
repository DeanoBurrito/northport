#include <tasking/Clock.h>
#include <arch/Platform.h>
#include <containers/Vector.h>
#include <debug/Log.h>
#include <interrupts/Ipi.h>
#include <Locks.h>

namespace Npk::Tasking
{
    struct ClockEvent
    {
        size_t nanosRemaining;
        void* data;
        void (*Callback)(void*);
        size_t period;
        size_t callbackCore;

        ClockEvent() = default;
        ClockEvent(size_t nanos, void* data, void (*cb)(void*), size_t period, size_t core)
        : nanosRemaining(nanos), data(data), Callback(cb), period(period), callbackCore(core)
        {}
    };

    volatile size_t uptimeMillis = 0;
    Npk::InterruptLock eventsLock;
    sl::Vector<ClockEvent> events; //TODO: implement sl::Queue<T>, use it here

    void ClockEventDispatch(size_t)
    {
        //TODO: we dont account for the time taken by the callback to run.
        //future timer events could expire while these are running, we'll need some sort of polling clock too?
        eventsLock.Lock();
        const ClockEvent event = events.Front();
        events.Erase(0);
        eventsLock.Unlock();

        if (event.Callback != nullptr)
        {
            if (event.callbackCore == CoreLocal().id)
                event.Callback(event.data);
            else
                Interrupts::SendIpiMail(event.callbackCore, event.Callback, event.data);
        }
        
        if (event.period != 0)
            QueueClockEvent(event.period, event.data, event.Callback, true, event.callbackCore);
        
        ASSERT(events.Size() > 0, "System clock has empty event queue. No time-keeping event?");
        SetSystemTimer(events[0].nanosRemaining, nullptr);
    }

    void UptimeEventCallback(void*)
    {
        __atomic_add_fetch(&uptimeMillis, 1, __ATOMIC_RELAXED);
    }

    void StartSystemClock()
    {
        events.EnsureCapacity(0x10);
        QueueClockEvent(1'000'000, nullptr, UptimeEventCallback, true); //time-keeping event
        SetSystemTimer(events[0].nanosRemaining, ClockEventDispatch);

        Log("System clock start: uptimeTick=1ms", LogLevel::Info);
    }

    void QueueClockEvent(size_t nanoseconds, void* payloadData, void (*callback)(void* data), bool periodic, size_t core)
    {
        if (core == (size_t)-1)
            core = CoreLocal().id;
        const size_t period = nanoseconds;

        sl::ScopedLock scopeLock(eventsLock);
        for (size_t i = 0; i < events.Size(); i++)
        {
            if (events[i].nanosRemaining < nanoseconds)
            {
                nanoseconds -= events[i].nanosRemaining;
                continue;
            }

            events[i].nanosRemaining -= nanoseconds;
            events.Emplace(i, nanoseconds, payloadData, callback, periodic ? period : 0, core);
            return;
        }

        events.EmplaceBack(nanoseconds, payloadData, callback, periodic ? period : 0, core);
    }

    size_t GetUptime()
    {
        return uptimeMillis;
    }
}
