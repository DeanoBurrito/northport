#include <KernelApi.hpp>
#include <Scheduler.hpp>
#include <hardware/Plat.hpp>
#include <hardware/Entry.hpp>

namespace Npk
{
    constexpr size_t AlarmFreeMs = 100;
    constexpr sl::TimePoint AlarmFreeInterval =
    { 
        AlarmFreeMs * (sl::TimePoint::Frequency / sl::TimeScale::Millis) 
    };

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

        const auto intent = reinterpret_cast<ClockQueue*>(1);
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

        auto queue = event->queue.Exchange(nullptr);
        if (queue == nullptr)
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
}
