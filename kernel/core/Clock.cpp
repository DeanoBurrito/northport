#include <core/Clock.h>
#include <hardware/Arch.h>
#include <hardware/Platform.h>
#include <hardware/Entry.h>
#include <core/Config.h>
#include <core/Log.h>
#include <core/WiredHeap.h>
#include <Maths.h>
#include <Locks.h>

namespace Npk::Core
{
    constexpr size_t DefaultSoftClockFreq = 100;
    constexpr size_t MinSoftClockFreq = 10;
    constexpr size_t MaxSoftClockFreq = 10000;
    constexpr size_t MaxAcceptableEventCount = 256;

    sl::Atomic<TimerNanos> uptimeOffset = 0;

    ClockEvent softClockEvent;
    DpcStore softClockDpc;
    sl::Atomic<size_t> softClockTicks;
    size_t softClockFreq;
    bool useSoftClock;

    struct ClockQueue
    {
        size_t coreId;
        sl::FwdList<ClockEvent, &ClockEvent::listHook> events;
        TimerNanos lastModified;
    };

    static void SoftClockTick(void* arg)
    {
        (void)arg;

        softClockTicks++;
        softClockEvent.expiry = sl::TimeCount(softClockFreq, 1);

        QueueClockEvent(&softClockEvent);
    }

    static void RefreshClockQueue(ClockQueue* q)
    {
        ASSERT_(CurrentRunLevel() >= RunLevel::Clock);

        const TimerNanos now = GetTimestamp();
        ASSERT_(now > q->lastModified);
        TimerNanos listDelta = now - q->lastModified;
        q->lastModified = now;

        for (auto it = q->events.Begin(); it != q->events.End(); ++it)
        {
            if (listDelta < it->expiry.ticks)
            {
                it->expiry.ticks -= listDelta;
                break;
            }

            listDelta -= it->expiry.ticks;
            it->expiry.ticks = 0;
        }
    }

    void InitLocalClockQueue()
    {
        ClockQueue* q = NewWired<ClockQueue>();
        ASSERT_(q != nullptr);

        q->coreId = CoreId();
        SetLocalPtr(SubsysPtr::ClockQueue, q);

        const TimerNanos uptimeBegin = GetTimestamp();
        TimerNanos expectedOffset = 0;
        if (uptimeOffset.CompareExchange(expectedOffset, uptimeBegin))
        {
            //we're the first core, do some initial setup:
            //- set the base count for hardware uptime (done in the CompareExchange)
            //- setup software based timekeeping if we must.
            Log("Set system uptime offset to %" PRIu64, LogLevel::Info, uptimeBegin);

            TimerCapabilities timerCaps {};
            GetTimeCapabilities(timerCaps);

            useSoftClock = !timerCaps.timestampForUptime || GetConfigNumber("kernel.clock.force_sw_uptime", false);

            //if the hardware cant provide a suitable timer for all cores to use,
            //we'll do it in software (although this is far from ideal).
            if (useSoftClock)
            {
                softClockFreq = sl::Clamp(GetConfigNumber("kernel.clock.uptime_freq", DefaultSoftClockFreq), 
                    MinSoftClockFreq, MaxSoftClockFreq);

                softClockDpc.data.function = SoftClockTick;
                softClockEvent.dpc = &softClockDpc;
                softClockEvent.expiry  = sl::TimeCount(softClockFreq, 1);
                QueueClockEvent(&softClockEvent);

                Log("Hardware unable to provide suitable timers for uptime, using softclock at %zuHz.", LogLevel::Info, softClockFreq);
            }
            else
                Log("System will use hardware clock for uptime", LogLevel::Info);
        }
    }

    sl::TimeCount GetUptime()
    { 
        if (useSoftClock)
            return sl::TimeCount(softClockFreq, softClockTicks.Load());

        if (!CoreLocalAvailable() || GetLocalPtr(SubsysPtr::ClockQueue) == nullptr)
            return {};

        const auto nanos = GetTimestamp() - uptimeOffset;
        ASSERT_(GetTimestamp() > uptimeOffset);
        return sl::TimeCount(sl::TimeScale::Nanos, nanos);
    }

    void QueueClockEvent(ClockEvent* event)
    {
        VALIDATE_(event != nullptr, );
        VALIDATE_(event->dpc != nullptr, );

        const auto prevRl = RaiseRunLevel(RunLevel::Clock);

        ClockQueue* q = static_cast<ClockQueue*>(GetLocalPtr(SubsysPtr::ClockQueue));
        ASSERT_(q != nullptr);
        event->expiry = event->expiry.Rebase(sl::Nanos);
        DispatchAlarm();

        if (q->events.Empty() || q->events.Front().expiry.ticks > event->expiry.ticks)
        {
            //inserting at front of list, re-arm interrupt timer
            if (!q->events.Empty())
                q->events.Front().expiry.ticks -= event->expiry.ticks;
            q->events.PushFront(event);

            const TimerNanos armTime = sl::Min(AlarmMax(), event->expiry.ticks);
            ASSERT_(armTime > 0);
            SetAlarm(armTime);

            LowerRunLevel(prevRl);
            return;
        }

        for (auto it = q->events.Begin(); it != q->events.End(); ++it)
        {
            event->expiry.ticks -= it->expiry.ticks;
            auto next = it;
            ++next;

            if (next != q->events.End() && next->expiry.ticks < event->expiry.ticks)
                continue;

            q->events.InsertAfter(it, event);
            break;
        }

        LowerRunLevel(prevRl);
    }

    bool DequeueClockEvent(ClockEvent* event)
    {
        VALIDATE_(event != nullptr, false);
        VALIDATE_(event->queue->coreId == CoreId(), false); //TODO: support this
        ASSERT_UNREACHABLE();
    }
}

namespace Npk
{
    using namespace Core;

    void DispatchAlarm()
    {
        ASSERT_(CurrentRunLevel() >= RunLevel::Clock);

        ClockQueue* q = static_cast<ClockQueue*>(GetLocalPtr(SubsysPtr::ClockQueue));
        ASSERT_(q != nullptr);
        size_t processedEvents = 0;

        RefreshClockQueue(q);
        while (!q->events.Empty() && q->events.Front().expiry.ticks == 0)
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
            const TimerNanos armTime = sl::Min(AlarmMax(), q->events.Front().expiry.ticks);
            ASSERT_(armTime > 0);
            SetAlarm(armTime);
        }
    }
}
