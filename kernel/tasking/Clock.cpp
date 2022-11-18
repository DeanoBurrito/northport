#include <tasking/Clock.h>
#include <arch/Platform.h>
#include <arch/Timers.h>
#include <containers/LinkedList.h>
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
    sl::LinkedList<ClockEvent> events;

    void ClockEventDispatch(size_t)
    {
dispatch_event:
        const size_t startTick = PollTimer();

        eventsLock.Lock();
        const ClockEvent event = events.PopFront();
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

        const size_t nanosPassed = PolledTicksToNanos(PollTimer() - startTick);
        if (nanosPassed >= events.Front().nanosRemaining || events.Front().nanosRemaining == 0)
            goto dispatch_event;
        else
            events.Front().nanosRemaining -= nanosPassed;
        
        ASSERT(events.Size() > 0, "System clock has empty event queue. No time-keeping event?");
        SetSysTimer(events.Front().nanosRemaining, nullptr);
    }

    void UptimeEventCallback(void*)
    {
        __atomic_add_fetch(&uptimeMillis, 1, __ATOMIC_RELAXED);
    }

    void StartSystemClock()
    {
        QueueClockEvent(1'000'000, nullptr, UptimeEventCallback, true); //time-keeping event
        SetSysTimer(events.Front().nanosRemaining, ClockEventDispatch);

        Log("System clock start: uptimeTick=1ms, sysTimer=%s, pollTimer=%s", LogLevel::Info, SysTimerName(), PollTimerName());
    }

    void QueueClockEvent(size_t nanoseconds, void* payloadData, void (*callback)(void* data), bool periodic, size_t core)
    {
        if (core == (size_t)-1)
            core = CoreLocal().id;
        const size_t period = nanoseconds;

        sl::ScopedLock scopeLock(eventsLock);
        for (auto it = events.Begin(); it != events.End(); ++it)
        {
            if (it->nanosRemaining < nanoseconds)
            {
                nanoseconds -= it->nanosRemaining;
                continue;
            }

            it->nanosRemaining -= nanoseconds;
            events.Insert(it, { nanoseconds, payloadData, callback, periodic ? period : 0, core });
            return;
        }

        events.EmplaceBack(nanoseconds, payloadData, callback, periodic ? period : 0, core);
    }

    size_t GetUptime()
    {
        return uptimeMillis;
    }
}
