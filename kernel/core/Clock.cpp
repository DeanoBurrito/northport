#include <private/Core.hpp>

namespace Npk
{
    struct ClockCancelRequest
    {
        sl::QueueMpScHook hook;
        ClockEvent* event;
        NpkStatus result;
        sl::Atomic<bool> hasResult;
    };

    using ClockCancelQueue = sl::QueueMpSc<ClockCancelRequest, 
        &ClockCancelRequest::hook>;

    struct ClockQueue
    {
        ClockList events;
        sl::TimePoint armedExpiry;
        sl::Atomic<bool> alarmArmed;
        sl::Atomic<bool> alarmPending;

        ClockStats stats;

        alignas(HwGetStaticCacheLineSize())
        ClockCancelQueue cancelRequests;
    };

    CPU_LOCAL(ClockQueue, clockQueue);

    //returns whether event was inserted at the front or not
    static bool InsertEvent(ClockQueue& queue, ClockEvent& event)
    {
        if (queue.events.Empty())
        {
            queue.events.PushFront(&event);

            return true;
        }

        if (event.expiry < queue.events.Front().expiry)
        {
            queue.events.PushFront(&event);

            return true;
        }

        if (queue.events.Back().expiry <= event.expiry)
        {
            queue.events.PushBack(&event);

            return false;
        }

        auto scan = queue.events.Begin();
        while (scan->expiry <= event.expiry)
            ++scan;
        queue.events.InsertBefore(scan, &event);

        return false;
    }

    NpkStatus ResetClockEvent(ClockEvent* event, sl::TimePoint expiry,
        sl::TimeCount period, const Completion& completion)
    {
        if (event == nullptr)
            return NpkStatus::InvalidArg;
        if (event->state.Load(sl::Acquire) == ClockEventState::Armed)
            return NpkStatus::Busy;

        if (period.ticks != 0 && period.frequency != 0)
        {
            const auto periodNs = period.Rebase(sl::Nanos).ticks;

            //period was too small or the conversion was bad, abort.
            if (periodNs == 0)
                return NpkStatus::InvalidArg;
            event->periodNs = periodNs;
        }
        else
            event->periodNs = 0;

        const auto target = completion.Get();
        event->completion.Set(target.data, target.type);
        event->expiry = expiry;

        event->state.Store(ClockEventState::Idle, sl::Release);

        return NpkStatus::Success;
    }

    NpkStatus AddClockEvent(ClockEvent* event)
    {
        if (event == nullptr)
            return NpkStatus::InvalidArg;
        if (event->expiry.epoch == 0)
            return NpkStatus::InvalidArg;

        auto prevIpl = EnsureIpl(Ipl::Alarm);
        auto& queue = *clockQueue;

        if (event->state.Load(sl::Relaxed) != ClockEventState::Idle)
        {
            if (prevIpl != Ipl::Alarm)
                LowerIpl(prevIpl);

            return NpkStatus::Busy;
        }

        event->owner = MyCoreId();
        event->state.Store(ClockEventState::Armed, sl::Release);
        if (InsertEvent(queue, *event))
            queue.alarmPending.Store(true, sl::Relaxed);

        queue.stats.Add(ClockStat::EventsArmed, 1);
        queue.stats.Add(ClockStat::QueueDepth, 1);

        if (prevIpl != Ipl::Alarm)
            LowerIpl(prevIpl);

        return NpkStatus::Success;
    }

    static NpkStatus DoCancel(ClockQueue& queue, ClockEvent& event)
    {
        switch (event.state.Load(sl::Relaxed))
        {
        case ClockEventState::Idle:
            return NpkStatus::NotAvailable;
        
        case ClockEventState::Expired:
            return NpkStatus::TooLate;

        case ClockEventState::Armed:
            break;
        }

        const bool wasFront = &queue.events.Front() == &event;
        queue.events.Remove(&event);
        event.state.Store(ClockEventState::Idle, sl::Relaxed);

        queue.stats.Add(ClockStat::EventsCancelled, 1);
        queue.stats.Sub(ClockStat::QueueDepth, 1);

        if (wasFront)
            queue.alarmPending.Store(true, sl::Relaxed);

        return NpkStatus::Success;
    }

    static void CancelMailCallback(void* arg)
    {
        auto* request = static_cast<ClockCancelRequest*>(arg);
        auto& queue = *clockQueue;

        queue.cancelRequests.Push(request);
        queue.alarmPending.Store(true, sl::Relaxed);
    }

    static NpkStatus CancelRemoteEvent(ClockEvent& event, CpuId owner)
    {
        ClockCancelRequest request {};
        request.event = &event;

        SmpMail mail {};
        ResetMail(&mail, CancelMailCallback, &request, {});

        SendMail(owner, &mail);

        while (!request.hasResult.Load(sl::Acquire))
            sl::HintSpinloop();

        return request.result;
    }

    NpkStatus CancelClockEvent(ClockEvent* event)
    {
        if (event == nullptr)
            return NpkStatus::InvalidArg;

        auto prevIpl = EnsureIpl(Ipl::Alarm);
        if (event->state.Load(sl::Acquire) == ClockEventState::Idle)
        {
            if (prevIpl != Ipl::Alarm)
                LowerIpl(prevIpl);

            return NpkStatus::NotAvailable;
        }

        const auto owner = event->owner;
        if (owner == MyCoreId())
        {
            auto result = DoCancel(*clockQueue, *event);
            if (prevIpl != Ipl::Alarm)
                LowerIpl(prevIpl);

            return result;
        }
        if (prevIpl != Ipl::Alarm)
            LowerIpl(prevIpl);

        if (CurrentIpl() != Ipl::Passive)
            return NpkStatus::Unsupported;

        return CancelRemoteEvent(*event, owner);
    }

    static void SetAlarmForQueue(ClockQueue& queue)
    {
        if (queue.events.Empty())
        {
            if (queue.alarmArmed.Exchange(false, sl::Relaxed))
                HwClearAlarm();

            return;
        }

        const auto expiry = queue.events.Front().expiry;
        if (queue.alarmArmed.Load(sl::Relaxed) && queue.armedExpiry == expiry)
            return;

        queue.armedExpiry = expiry;
        queue.alarmArmed.Store(true, sl::Relaxed);
        HwSetAlarm(expiry);

        queue.stats.Add(ClockStat::TimerArms, 1);
    }

    void Private::OnAlarmIpl()
    {
        NPK_ASSERT(CurrentIpl() == Ipl::Alarm);

        auto& queue = *clockQueue;
        bool shouldRearm = false;

        while (queue.alarmPending.Exchange(false, sl::Acquire))
        {
            shouldRearm = true;

            while (auto* req = queue.cancelRequests.Pop())
            {
                req->result = DoCancel(queue, *req->event);
                req->hasResult.Store(true, sl::Release);

                queue.stats.Add(ClockStat::RemoteCancels, 1);
            }

            queue.stats.Add(ClockStat::AlarmPasses, 1);

            ClockList expired {};
            size_t expiredCount = 0;
            const auto now = HwReadTimestamp();

            while (!queue.events.Empty())
            {
                if (queue.events.Front().expiry > now)
                    break;

                auto* event = queue.events.PopFront();
                expired.PushBack(event);
                expiredCount++;
            }

            while (!expired.Empty())
            {
                auto* event = expired.PopFront();

                if (event->periodNs != 0)
                {
                    event->expiry.epoch += event->periodNs;
                    if (event->expiry.epoch <= now.epoch)
                    {
                        queue.stats.Add(ClockStat::PeriodsMissed, 1);
                        event->expiry = { now.epoch + event->periodNs };
                    }

                    event->state.Store(ClockEventState::Armed, sl::Relaxed);
                    InsertEvent(queue, *event);
                    queue.stats.Add(ClockStat::QueueDepth, 1);

                    const auto completion = event->completion.Get();
                    NotifyCompletion(completion);
                    continue;
                }

                const auto completion = event->completion.Get();
                event->state.Store(ClockEventState::Expired, sl::Release);

                NotifyCompletion(completion);
            }

            queue.stats.Add(ClockStat::EventsExpired, expiredCount);
            queue.stats.Sub(ClockStat::QueueDepth, expiredCount);
            if (expiredCount == 0)
                queue.stats.Add(ClockStat::EmptyPasses, 1);
        }

        if (shouldRearm)
            SetAlarmForQueue(queue);
    }

    bool Private::AlarmIplHasPendingWork()
    {
        return clockQueue->alarmPending.Load(sl::Relaxed);
    }

    void DispatchAlarm()
    {
        AssertIpl(Ipl::Interrupt);

        clockQueue->alarmArmed.Store(false, sl::Relaxed);
        clockQueue->alarmPending.Store(true, sl::Release);
    }

    sl::Opt<sl::TimePoint> NextClockEvent()
    {
        const auto prevIpl = EnsureIpl(Ipl::Alarm);

        sl::Opt<sl::TimePoint> expiry {};
        if (!clockQueue->events.Empty())
            expiry = clockQueue->events.Front().expiry;

        if (prevIpl != Ipl::Alarm)
            LowerIpl(prevIpl);

        return expiry;
    }
}
