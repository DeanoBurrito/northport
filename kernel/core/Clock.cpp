#include <core/Clock.h>
#include <arch/Misc.h>
#include <arch/Timers.h>
#include <core/Config.h>
#include <core/Log.h>
#include <core/WiredHeap.h>
#include <Maths.h>
#include <Locks.h>

namespace Npk::Core
{
    constexpr size_t DefaultUptimeFrequency = 100;
    constexpr size_t MaxUptimeFrequency = 10000;
    constexpr size_t MinUptimeFrequency = 1;
    constexpr size_t MaxAcceptableEventCount = 256;

    sl::ScaledTime uptimePeriod;
    sl::Atomic<size_t> uptimeTicks;
    ClockEvent uptimeEvent;
    DpcStore uptimeDpcStore;
    
    struct ClockQueue
    {
        size_t coreId;
        sl::FwdList<ClockEvent, &ClockEvent::listHook> events;
        TimerTickNanos lastModified;
    };

    static void RefreshClockQueue(ClockQueue* q)
    {
        ASSERT_(CurrentRunLevel() >= RunLevel::Clock);

        const TimerTickNanos now = ReadPollTimer();
        TimerTickNanos listDelta = now - q->lastModified;
        q->lastModified = now;

        for (auto it = q->events.Begin(); it != q->events.End(); ++it)
        {
            if (it->expiry.units == 0)
                continue;

            if (listDelta < it->expiry.units)
            {
                it->expiry.units -= listDelta;
                break;
            }

            listDelta -= it->expiry.units;
            it->expiry.units = 0;
        }
    }

    static void UptimeTick(void* ignored)
    {
        (void)ignored;
        uptimeTicks.Add(1, sl::Relaxed);

        if (uptimeTicks.Load() % DefaultUptimeFrequency == 0)
            Log("Tick!", LogLevel::Debug);

        uptimeEvent.expiry = uptimePeriod;
        QueueClockEvent(&uptimeEvent);
    }

    void InitLocalClockQueue(bool startUptime)
    {
        ClockQueue* q = NewWired<ClockQueue>();
        ASSERT_(q != nullptr);

        q->coreId = CoreLocalId();
        SetLocalPtr(SubsysPtr::ClockQueue, q);

        if (startUptime)
        {
            VALIDATE_(uptimePeriod.units == 0, );

            size_t uptimeFreq = GetConfigNumber("kernel.clock.uptime_freq", DefaultUptimeFrequency);
            uptimeFreq = sl::Clamp(uptimeFreq, MinUptimeFrequency, MaxUptimeFrequency);
            uptimePeriod = sl::ScaledTime::FromFrequency(uptimeFreq);
            Log("Uptime count frequency: %zuHz", LogLevel::Info, uptimeFreq);

            uptimeTicks = 0;
            uptimeEvent.expiry = uptimePeriod;
            uptimeEvent.dpc = &uptimeDpcStore;
            uptimeDpcStore.data.function = UptimeTick;

            QueueClockEvent(&uptimeEvent);
        }
    }

    void ProcessLocalClock()
    {
        ASSERT_(CurrentRunLevel() >= RunLevel::Clock);

        ClockQueue* q = static_cast<ClockQueue*>(GetLocalPtr(SubsysPtr::ClockQueue));
        ASSERT_(q != nullptr);
        size_t processedEvents = 0;

        RefreshClockQueue(q);
        while (!q->events.Empty() && q->events.Front().expiry.units == 0)
        {
            if (processedEvents == MaxAcceptableEventCount)
            {
                Log("Too many timer events expiring at once, delaying expiry actions to avoid livelock", LogLevel::Error);
                break;
            }

            processedEvents++;
            ClockEvent* expired = q->events.PopFront();
            QueueDpc(expired->dpc);
        }

        if (!q->events.Empty())
        {
            const TimerTickNanos armTime = sl::Min(MaxIntrTimerExpiry(), q->events.Front().expiry.units);
            ASSERT_(ArmIntrTimer(armTime));
        }
    }

    sl::ScaledTime GetUptime()
    { 
        const auto ticks = uptimeTicks.Load(sl::Relaxed);
        return sl::ScaledTime(uptimePeriod.scale, uptimePeriod.units * ticks);
    }

    void QueueClockEvent(ClockEvent* event)
    {
        VALIDATE_(event != nullptr, );
        VALIDATE_(event->dpc != nullptr, );

        ASSERT_(CurrentRunLevel() < RunLevel::Clock);
        const auto prevRl = RaiseRunLevel(RunLevel::Clock);

        ClockQueue* q = static_cast<ClockQueue*>(GetLocalPtr(SubsysPtr::ClockQueue));
        ASSERT_(q != nullptr);
        RefreshClockQueue(q);
        event->expiry = event->expiry.ToScale(sl::TimeScale::Nanos);

        if (q->events.Empty() || q->events.Front().expiry.units > event->expiry.units)
        {
            //inserting at front of list, re-arm interrupt timer
            if (!q->events.Empty())
                q->events.Front().expiry.units -= event->expiry.units;
            q->events.PushFront(event);

            const TimerTickNanos armTime = sl::Min(MaxIntrTimerExpiry(), event->expiry.units);
            ASSERT_(ArmIntrTimer(armTime));

            LowerRunLevel(prevRl);
            return;
        }

        for (auto it = q->events.Begin(); it != q->events.End(); ++it)
        {
            event->expiry.units -= it->expiry.units;

            auto next = static_cast<ClockEvent*>(it->listHook.next);
            if (next != nullptr && next->expiry.units < event->expiry.units)
                continue;

            q->events.InsertAfter(it, event);
            if (next != nullptr)
                next->expiry.units -= event->expiry.units;
        }

        LowerRunLevel(prevRl);
    }

    void DequeueClockEvent(ClockEvent* event)
    {
        ASSERT_UNREACHABLE();
    }
}
