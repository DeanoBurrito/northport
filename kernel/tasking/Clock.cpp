#include <tasking/Clock.h>
#include <arch/Platform.h>
#include <arch/Timers.h>
#include <containers/LinkedList.h>
#include <debug/Log.h>
#include <interrupts/Ipi.h>
#include <memory/Heap.h>
#include <Locks.h>

namespace Npk::Tasking
{
    constexpr size_t ClockTickMs = sl::Clamp<size_t>(NP_CLOCK_MS, 1, 20);
    
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
    sl::LinkedList<ClockEvent, Memory::CachingSlab<32>> events;

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
        __atomic_add_fetch(&uptimeMillis, ClockTickMs, __ATOMIC_RELAXED);
    }

    void StartSystemClock()
    {
        QueueClockEvent(ClockTickMs * 1'000'000, nullptr, UptimeEventCallback, true); //main timekeeping event.
        SetSysTimer(events.Front().nanosRemaining, ClockEventDispatch);

        Log("System clock start: tick=%lums, sysTimer=%s, pollTimer=%s", LogLevel::Info, ClockTickMs, SysTimerName(), PollTimerName());
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

            //TODO: what if we insert before the first event (which is timer expiry), 
            //we should reset the timer expiry.
            events.Insert(it, { nanoseconds, payloadData, callback, periodic ? period : 0, core });
            return;
        }

        /* Handling an unlikely scenario: if a clock event is set *far* into the future,
        beyond what the hardware timer can handle in one cycle, we add events just
        below the limit of the timer. This trades clock counter length for memory usage,
        and effectively allows for infinite expiry times (memory permitting).
        */
        const size_t maxTimerNanos = SysTimerMaxNanos();
        while (nanoseconds >= maxTimerNanos)
        {
            events.EmplaceBack(maxTimerNanos, nullptr, nullptr, 0ul, 0ul);
            nanoseconds -= maxTimerNanos;
        }

        events.EmplaceBack(nanoseconds, payloadData, callback, periodic ? period : 0, core);
    }

    size_t GetUptime()
    {
        return uptimeMillis;
    }
}
