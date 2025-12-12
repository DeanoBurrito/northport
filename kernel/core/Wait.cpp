#include <CorePrivate.hpp>
#include <Maths.hpp>

/* wait subsystem notes:
 * - signalling only wakes a number of blocked threads, it doesnt satisfy
 *   anyone. Therefore waitentries are only touched by the blocking thread
 *   and need no synchronization.
 * - link to keyronex
 */
namespace Npk
{
    constexpr size_t ResetMaxFails = 4;

    //NOTE: assumes entry->waitable is locked
    static bool TryAcquireWaitable(WaitEntry* entry)
    {
        if (entry->waitable->tickets == 0)
        {
            if (entry->waitable->type == WaitableType::Condition)
            {
                entry->waitable->waiters.Remove(entry);
                return true;
            }

            return false;
        }

        entry->satisfied = true;
        entry->waitable->waiters.Remove(entry);

        //perform any processing the waitable requires
        switch (entry->waitable->type)
        {
        case WaitableType::Timer: 
            break;
        case WaitableType::Mutex:
            entry->waitable->tickets = 0;
            entry->waitable->mutexHolder = entry->thread;
            break;
        default:
            NPK_UNREACHABLE();
        }

        return true;
    }

    //NOTE: assumes waitable is locked, returns number of possible acquisitions
    static size_t SetWaitableSignalled(Waitable* what)
    {
        switch (what->type)
        {
        case WaitableType::Condition:
            what->tickets--;
            return static_cast<size_t>(~0);

        case WaitableType::Timer:
            what->tickets = 1;
            return static_cast<size_t>(~0);

        case WaitableType::Mutex:
            what->tickets++;
            return what->tickets;

        default:
            NPK_UNREACHABLE();
        }
    }

    static bool StopWait(ThreadContext* thread, WaitStage why)
    {
        auto& waiter = thread->waiting;

        auto& stage = waiter.stage;
        auto preparing = WaitStage::Preparing;
        auto blocking = WaitStage::Blocked;

        if (stage.CompareExchange(preparing, why, sl::AcqRel))
        {
            //stopped an early wait, the thread will detect this and cleanup
            //on its own.
            return true;
        }
        else if (stage.CompareExchange(blocking, why, sl::AcqRel))
        {
            //stopped an in-progress wait, we'll need to make the thread
            //runnable again.
            Private::WakeThread(thread);
            return true;
        }

        //wait is in some other non-stoppable state.
        return false;
    }

    static void WaitTimeoutCallback(Dpc* dpc, void* arg)
    {
        (void)dpc;

        auto thread = static_cast<ThreadContext*>(arg);
        StopWait(thread, WaitStage::Timedout);
    }

    bool CancelWait(ThreadContext* thread)
    {
        NPK_CHECK(thread != nullptr, false);
        return StopWait(thread, WaitStage::Cancelled);
    }

    WaitStatus WaitMany(sl::Span<Waitable*> what, WaitEntry* entries, 
        sl::TimeCount timeout, sl::StringSpan reason)
    {
        NPK_CHECK(!what.Empty(), WaitStatus::Success);
        NPK_CHECK(entries != nullptr, WaitStatus::Incomplete);
        AssertIpl(Ipl::Passive);

        auto thread = GetCurrentThread();
        auto& waiter = thread->waiting;

        waiter.lock.Lock();
        waiter.reason = reason;
        waiter.lock.Unlock();

        waiter.stage.Store(WaitStage::Preparing, sl::Release);
        
        bool satisfied = false;
        for (size_t i = 0; i < what.Size(); i++)
        {
            auto& e = entries[i];
            e.waitable = what[i];
            e.thread = thread;
            e.satisfied = false;

            e.waitable->lock.Lock();
            e.waitable->waiters.PushBack(&e);
            if (TryAcquireWaitable(&e))
            {
                waiter.stage.Store(WaitStage::Satisfied, sl::Release);
                satisfied = true;
            }
            e.waitable->lock.Unlock();
        }

        //if we're already satisfied or this was only a poll, undo our
        //preparations and return to the caller.
        if (satisfied || timeout.ticks == 0)
        {
            for (size_t i = 0; i < what.Size(); i++)
            {
                auto& e = entries[i];

                e.waitable->lock.Lock();
                if (!e.satisfied)
                    e.waitable->waiters.Remove(&e);
                e.waitable->lock.Unlock();

                e.thread = nullptr;
                e.waitable = nullptr;
            }

            auto preparing = WaitStage::Preparing;
            auto desired = WaitStage::Timedout;
            if (waiter.stage.CompareExchange(preparing, desired, sl::AcqRel))
                return WaitStatus::Timedout;

            const auto stage = waiter.stage.Load(sl::Acquire);
            switch (stage)
            {
            case WaitStage::Satisfied:  return WaitStatus::Success;
            case WaitStage::Reset:      return WaitStatus::Reset;
            case WaitStage::Cancelled:  return WaitStatus::Cancelled;
            default: break;
            }
            NPK_UNREACHABLE();
        }

        Dpc timeoutDpc {};
        timeoutDpc.arg = thread;
        timeoutDpc.function = WaitTimeoutCallback;
        
        ClockEvent timeoutEvent {};
        timeoutEvent.dpc = &timeoutDpc;
        timeoutEvent.expiry = GetMonotonicTime() + timeout;
        if (timeout != sl::NoTimeout)
            AddClockEvent(&timeoutEvent);

        auto prevIpl = RaiseIpl(Ipl::Dpc);
        Private::BeginWait();

        auto expected = WaitStage::Preparing;
        auto desired = WaitStage::Blocked;
        bool earlySatisfaction = waiter.stage.CompareExchange(expected, 
            desired, sl::AcqRel);

        WaitStatus status;
        while (true)
        {
            if (!earlySatisfaction)
            {
                earlySatisfaction = false;
                //TODO: try spin for a bit and then yield() manually,
                //instead of BeginWait() setting switchPending=true and
                //LowerIpl() allowing the rescheduler to take place.
                LowerIpl(prevIpl); //allows preemption to take place
                prevIpl = RaiseIpl(Ipl::Dpc);
            }

            //we've been woken up, check for any special conditions, otherwise
            //try to satisfy the wait.
            bool satisfied = false;

            const auto stage = waiter.stage.Load(sl::Acquire);
            switch (stage)
            {
            case WaitStage::Reset:
                status = WaitStatus::Reset;
                satisfied = true;
                break;
            case WaitStage::Cancelled:
                status = WaitStatus::Cancelled;
                satisfied = true;
                break;
            case WaitStage::Timedout:
                status = WaitStatus::Timedout;
                satisfied = true;
                break;
            default: 
                break;
            }
            if (satisfied)
                break;

            //we didnt wake for any special cases, try to acquire a waitable
            for (size_t i = 0; i < what.Size(); i++)
            {
                what[i]->lock.Lock();
                satisfied |= TryAcquireWaitable(&entries[i]);
                what[i]->lock.Lock();
            }

            if (satisfied)
            {
                status = WaitStatus::Success;
                break;
            }
        }

        Private::EndWait();

        for (size_t i = 0; i < what.Size(); i++)
        {
            auto& e = entries[i];

            e.waitable->lock.Lock();
            if (!e.satisfied)
                e.waitable->waiters.Remove(&e);
            e.waitable->lock.Unlock();

            e.thread = nullptr;
            e.waitable = nullptr;
        }

        //TODO: we'll need to handle an edge case here in the future. If the
        //timeout event has fired we will fail to cancel it, so we need to sync
        //with it executing before returning from this function (since the dpc
        //object is stack-allocated).
        NPK_ASSERT(RemoveClockEvent(&timeoutEvent));

        waiter.lock.Lock();
        waiter.reason = {};
        waiter.lock.Unlock();

        return status;
    }

    CPU_LOCAL(WaitableMpScQueue, static waitablesPendingSignal);
    
    CPU_LOCAL_CTOR(
    {
        new (waitablesPendingSignal.Get()) WaitableMpScQueue();
    });

    static void SignalWaitableInternal(Waitable* what)
    {
        AssertIpl(Ipl::Dpc);

        what->lock.Lock();

        const size_t wakeCount = SetWaitableSignalled(what);
        for (size_t i = 0; i < wakeCount; i++)
        {
            auto waiter = what->waiters.PopFront();
            if (waiter == nullptr)
                break;

            auto preparing = WaitStage::Preparing;
            auto blocking = WaitStage::Blocked;
            const auto desired = WaitStage::Satisfied;
            
            auto& stage = waiter->thread->waiting.stage;
            if (stage.CompareExchange(preparing, desired, sl::AcqRel))
            {} //no-op, the thread will detect it was satisfied
            else if (stage.CompareExchange(blocking, desired, sl::AcqRel))
                Private::WakeThread(waiter->thread);
        }
        what->lock.Unlock();
    }

    void SignalWaitable(Waitable* what)
    {
        NPK_CHECK(what != nullptr, );

        waitablesPendingSignal->Push(what);

        if (CurrentIpl() == Ipl::Passive)
        {
            //waitable signalling actually happens when local IPL is lowered to
            //passive. If we're already at passive, raise the ipl and them lower
            //it again to trigger that condition.
            auto prev = RaiseIpl(Ipl::Dpc);
            LowerIpl(prev);
        }
    }

    //NOTE: called at Ipl::Dpc with interrupts enabled, when about to lower
    //to Ipl::Passive.
    void Private::PrePassiveRunLevel()
    {
        Waitable* pending = nullptr;
        while ((pending = waitablesPendingSignal->Pop()) != nullptr)
            SignalWaitableInternal(pending);
    }

    bool ResetWaitable(Waitable* what, WaitableType newType, size_t tickets)
    {
        NPK_CHECK(what != nullptr, false);

        bool canReset = false;
        for (size_t i = 0; i < ResetMaxFails; i++)
        {
            what->lock.Lock();
            switch (what->type)
            {
            case WaitableType::Condition:
                canReset = true;
                break;

            case WaitableType::Timer:
                canReset = RemoveClockEvent(&what->clockEvent);
                break;

            case WaitableType::Mutex:
                canReset = what->mutexHolder == nullptr;
                break;

            default:
                NPK_UNREACHABLE();
            }

            if (canReset)
                break;
            what->lock.Unlock();
        }

        if (!canReset)
            return false;

        while (true)
        {
            auto waiter = what->waiters.PopFront();
            if (waiter == nullptr)
                break;

            StopWait(waiter->thread, WaitStage::Reset);
        }

        switch (what->type)
        {
        case WaitableType::Condition: 
            break;

        case WaitableType::Timer:
            what->clockEvent.waitable = what;
            break;

        case WaitableType::Mutex:
            what->mutexHolder = nullptr;
            break;
        }

        what->tickets = tickets;
        what->type = newType;
        what->lock.Unlock();

        return true;
    }
}
