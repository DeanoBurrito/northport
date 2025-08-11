#include <CoreApi.hpp>
#include <hardware/Plat.hpp>
#include <hardware/Entry.hpp>

/* Clock Subsystem:
 * There's a few things happening in this file:
 * - The first is 'cycle accounting', a name shamelessly stolen from minoca
 *   sources. This is just tracking who's been using cpu processing time.
 * - The second is the clock queue(s). This allows us to multiplex 1 hardware
 *   timer (that provides a timestamp + interrupt capabilities) into as many
 *   events as we need in software. All clock queue related code runs at DPC
 *   IPL. Each cpu core maintains its own clock queue, which is a lock and
 *   a doubly linked list of clock events, sorted with the soonest-expiring
 *   first. Generally a cpu only managed it's local clock queue, with the
 *   exception being cancelling a queued clock event. Since a thread might
 *   have migrated between starting and wanting to stop a clock event, we need
 *   to support this, hence why each clock queue has a lock.
 * - The third is correlating real world time with system time, we do this
 *   via `systemTimeOffset` which is added to the platform provided timestamp.
 */
namespace Npk
{
    constexpr size_t AlarmFreeMs = 100;
    constexpr sl::TimePoint AlarmFreeInterval =
    { 
        AlarmFreeMs * (sl::TimePoint::Frequency / sl::TimeScale::Millis) 
    };

    //NOT a real queue! we need a magic value for some logic down below, that
    //is non-null.
    static ClockQueue* const enqueuing = 
        reinterpret_cast<ClockQueue*>(alignof(ClockQueue*));

    struct CycleAccounting
    {
        IntrSpinLock lock;
        CycleAccount account;
        sl::TimePoint periodBegin;

        uint64_t user;
        uint64_t kernel;
        uint64_t kernelInterrupt;
        uint64_t driver;
        uint64_t driverInterrupt;
        uint64_t debugger;
    };

    CPU_LOCAL(CycleAccounting, accounting);

    CPU_LOCAL(ClockQueue, clockQueue);
    CPU_LOCAL(Dpc, clockDpc);
    CPU_LOCAL(sl::Atomic<bool>, clockDpcPending);

    CycleAccount SetCycleAccount(CycleAccount who)
    {
        sl::ScopedLock scopeLock(accounting->lock);

        auto currentThread = GetCurrentThread();
        const auto now = PlatReadTimestamp();
        const auto period = accounting->periodBegin - now;
        NPK_ASSERT(period.Frequency == sl::Nanos);

        switch (accounting->account)
        {
        case CycleAccount::User:
            if (currentThread != nullptr)
                currentThread->accounting.userNs += period.epoch;
            accounting->user += period.epoch;
            break;
        case CycleAccount::Kernel:
            if (currentThread != nullptr)
                currentThread->accounting.kernelNs += period.epoch;
            accounting->kernel += period.epoch;
            break;
        case CycleAccount::KernelInterrupt:
            accounting->kernelInterrupt += period.epoch;
            break;
        case CycleAccount::Driver:
            //TODO: update active driver block
            accounting->driver += period.epoch;
            break;
        case CycleAccount::DriverInterrupt:
            accounting->driverInterrupt += period.epoch;
            break;
        case CycleAccount::Debugger:
            accounting->debugger += period.epoch;
            break;
        default:
            NPK_UNREACHABLE();
        }

        const auto prevAccount = accounting->account;
        accounting->account = who;
        accounting->periodBegin = now;

        return prevAccount;
    }

    //NOTE: clockQueue->lock is held
    static void ArmLocalAlarm()
    {
        if (!clockQueue->events.Empty())
            return PlatSetAlarm(clockQueue->events.Front().expiry);

        Log("Empty clock queue, alarm set for free interval of %zums", 
            LogLevel::Info, AlarmFreeMs);
        PlatSetAlarm(AlarmFreeInterval);
    }

    static void UpdateClockQueue(Dpc* self, void* arg)
    {
        (void)self;
        (void)arg;

        clockDpcPending->Store(false);
        ClockList expiredEvents {};

        clockQueue->lock.Lock();
        while (!clockQueue->events.Empty())
        {
            if (clockQueue->events.Front().expiry > PlatReadTimestamp())
                break;

            auto expired = clockQueue->events.PopFront();
            expired->queue.Store(nullptr, sl::Release);
            expiredEvents.PushBack(expired);
        }

        ArmLocalAlarm();
        clockQueue->lock.Unlock();

        while (!expiredEvents.Empty())
        {
            auto event = expiredEvents.PopFront();

            if (event->dpc != nullptr)
                QueueDpc(event->dpc);
            if (event->waitable != nullptr)
                SignalWaitable(event->waitable);
        }
    }

    static void QueueClockDpc()
    {
        if (!clockDpcPending->Exchange(true))
            return;

        clockDpc->function = UpdateClockQueue;
        QueueDpc(&*clockDpc);
    }

    void AddClockEvent(ClockEvent* event)
    {
        NPK_ASSERT(event != nullptr);
        NPK_ASSERT(event->dpc != nullptr || event->waitable != nullptr);

        //NOTE: there's no grace period for this comparison, if the event expires
        //between now and the time of adding it to the queue and re-arming
        //the alarm, the alarm code is guarenteed to handle this for us,
        //and should call `DispatchAlarm()`. Most of this time can actually
        //be handled by hardware.
        //This allows the logic here to be simpler, and we dont have to decide on
        //an arbitary grace period.
        if (event->expiry < (PlatReadTimestamp()))
        {
            if (event->dpc != nullptr)
                QueueDpc(event->dpc);
            if (event->waitable != nullptr)
                SignalWaitable(event->waitable);
            return;
        }

        const auto intent = enqueuing;
        event->queue.Store(intent, sl::Release);
        clockQueue->lock.Lock();
        auto prevQueue = event->queue.Exchange(&*clockQueue, sl::Acquire);

        if (prevQueue == intent)
        {
            clockQueue->events.InsertSorted(event, 
                [](auto* a, auto* b) 
                { 
                    return a->expiry < b->expiry; 
                });
            ArmLocalAlarm();
        }
        clockQueue->lock.Unlock();
    }

    bool RemoveClockEvent(ClockEvent* event)
    {
        NPK_ASSERT(event != nullptr);

        auto queue = event->queue.Exchange(nullptr, sl::AcqRel);
        if (queue == nullptr || queue == enqueuing)
            return false;

        queue->lock.Lock();
        NPK_ASSERT(!queue->events.Empty());

        const bool isFirst = &queue->events.Front() == event;
        queue->events.Remove(event);

        if (isFirst && queue == &*clockQueue)
            ArmLocalAlarm();
        queue->lock.Unlock();

        return true;
    }

    //Called by the hardware layer when the hardware alarm interrupt has fired,
    //from within the interrupt handler.
    void DispatchAlarm()
    {
        QueueClockDpc();
    }

    static sl::Atomic<sl::TimePoint> systemTimeOffset {};

    sl::TimePoint GetTime()
    {
        return PlatReadTimestamp() + systemTimeOffset.Load(sl::Relaxed);
    }

    sl::TimePoint GetTimeOffset()
    {
        return systemTimeOffset.Load(sl::Relaxed);
    }

    void SetTimeOffset(sl::TimePoint offset)
    {
        auto prev = systemTimeOffset.Exchange(offset, sl::Relaxed);

        const auto dirStr = offset.epoch > prev.epoch ? "+" : "-";
        const auto diff = offset.epoch > prev.epoch ? offset.epoch - prev.epoch
            : prev.epoch - offset.epoch;
        auto date = sl::CalendarPoint::From(offset);

        Log("System time offset set: %s%zu, new base is %02u/%02u/%02" 
            PRIu32" %02u:%02u.%02u",
            LogLevel::Info, dirStr, diff, date.dayOfMonth, date.month, 
            date.year, date.hour, date.minute, date.second);
    }
}
