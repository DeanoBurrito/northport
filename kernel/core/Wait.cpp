#include <private/Core.hpp>

/* Wait component design:
 * - 3 main phase: mark waitable as pending, process pending waitables (wake
 *   some amount of threads), awoken treads try acquire waitables.
 * - sxmutex single word for CAS
 * - signal/release side atomically updates state, then wakes threads.
 * - acquire/wait side attempts to atomically manipulate the same state.
 * - lock convoys avoided by not doing atomic mutex handoff, most of the time.
 * - some atomic handoff to avoid starvation of the waiter queue.
 */
namespace Npk
{
    //In order to keep the fast paths lock-free we need to be able to CAS
    //all of the following fields. This struct gets packed into a single atomic
    //word (`state`) in the waitable; this type allows the fields to be
    //accessed conviniently.
    struct SxMutexState
    {
        size_t heldExclusive : 1;
        size_t exclusiveWaiters : 15;
        size_t sharedHolders : (sizeof(size_t) * 8) - 16;
    };
    static_assert(sizeof(SxMutexState) == sizeof(size_t));

    CPU_LOCAL(WaitableMpScQueue, static pendingWaitables);

    CPU_LOCAL_CTOR(
    {
        new (pendingWaitables.Get()) WaitableMpScQueue();
    });

    static void AdjustExclusiveCount(SxMutex* sx, bool increment)
    {
        while (true)
        {
            size_t state = sx->tickets.Load(sl::Acquire);
            SxMutexState sxState;
            sl::MemCopy(&sxState, &state, sizeof(sxState));

            if (increment)
                sxState.exclusiveWaiters++;
            else
             sxState.exclusiveWaiters--;

            size_t desired;
            sl::MemCopy(&desired, &sxState, sizeof(state));

            if (sx->tickets.CompareExchange(state, desired, sl::AcqRel))
                break;
        }
    }

    //NOTE: called at Ipl::Dpc with interrupts enabled, when about to lower
    //to Ipl::Passive.
    void Private::PrePassiveRunLevel()
    {
        Waitable* pending = nullptr;
        while ((pending = pendingWaitables->Pop()) != nullptr)
        {
            pending->pending.Store(false, sl::Release);

            pending->listLock.Lock();
            auto& waiters = pending->waitersList;

            switch (pending->type)
            {
            case WaitableType::Condition:
                [[fallthrough]];
            case WaitableType::Timer:
            {
                //conditions and timers are the same under the hood, just one is
                //signalled manually and one is signalled by the clock.
                if (pending->tickets.Load(sl::Acquire) != 0)
                    break;

                //tickets == 0, wake everyone, good luck scheduler.
                for (auto it = waiters.Begin(); it != waiters.End(); ++it)
                    WakeThread(it->thread);

                break;
            }

            case WaitableType::Mutex:
            {
                //wake the first min(tickets, waiterCount) threads
                size_t tickets = pending->tickets.Load(sl::Acquire);
                for (auto it = waiters.Begin(); it != waiters.End(); ++it)
                {
                    if (tickets == 0)
                        break;

                    WakeThread(it->thread);
                    tickets--;
                }

                break;
            }

            case WaitableType::SxMutex:
            {
                if (waiters.Empty())
                    break;

                const size_t state = pending->tickets.Load(sl::Acquire);
                SxMutexState sxState;
                sl::MemCopy(&sxState, &state, sizeof(sxState));

                auto& head = waiters.Front();
                if (head.isExclusive && !sxState.heldExclusive
                    && sxState.sharedHolders == 0)
                {
                    //no shared holders, no exclusive holder and first waiter
                    //wants an exclusive acquire: wake just the list head.
                    WakeThread(head.thread);
                    break;
                }
                else if (!head.isExclusive && !sxState.heldExclusive)
                {
                    //no exclusive holder for the lock and the first waiter
                    //wants shared access: wake all shared waiters at the front
                    //of the queue.
                    for (auto it = waiters.Begin(); it != waiters.End(); ++it)
                    {
                        if (it->isExclusive)
                            break;

                        WakeThread(it->thread);
                    }
                }

                break;
            }

            default:
                NPK_UNREACHABLE();
            }
            pending->listLock.Unlock();
        }
    }

    static void QueueWaitable(Waitable* what)
    {
        NPK_ASSERT(what != nullptr);

        const auto prevPending = what->pending.Exchange(true, sl::AcqRel);
        if (prevPending)
        {
            //waitable is already in a pending queue somewhere, to avoid
            //trampling the hook and corruption their queue we do nothing here.
            //This is fine as the waitable will be looked at eventually since
            //it's in a pending list.
            return;
        }

        pendingWaitables->Push(what);

        //bump IPL to DPC and back if we need to, to trigger draing
        //the local pending queue of waitables.
        if (CurrentIpl() < Ipl::Dpc)
        {
            auto currIpl = RaiseIpl(Ipl::Dpc);
            LowerIpl(currIpl);
        }
    }

    NpkStatus CancelWait(ThreadContext* thread)
    {
        if (thread == nullptr)
            return NpkStatus::InvalidArg;

        auto& waiter = thread->waiting;

        auto preparing = WaitStage::Preparing;
        auto blocked = WaitStage::Blocked;
        const auto desired = WaitStage::Cancelled;

        if (waiter.stage.CompareExchange(preparing, desired, sl::AcqRel))
            return NpkStatus::Success;

        if (waiter.wakeDpc == nullptr)
            return NpkStatus::InternalError;
        if (waiter.stage.CompareExchange(blocked, desired, sl::AcqRel))
        {
            //QueueDpc() is the secret ingredient that makes calling CancelWait
            //safe from any IPL.
            QueueDpc(waiter.wakeDpc);

            return NpkStatus::Success;
        }

        return NpkStatus::NotAvailable;
    }

    static void WakeThreadDpc(Dpc* self, void* arg)
    {
        (void)self;

        auto* thread = static_cast<ThreadContext*>(arg);
        NPK_ASSERT(thread != nullptr);

        Private::WakeThread(thread);
    }

    static void WaitTimeoutDpc(Dpc* self, void* arg)
    {
        (void)self;

        auto* thread = static_cast<ThreadContext*>(arg);
        NPK_ASSERT(thread != nullptr);
        auto& waiter = thread->waiting;

        auto preparing = WaitStage::Preparing;
        auto blocked = WaitStage::Blocked;
        const auto desired = WaitStage::Timedout;
        
        if (waiter.stage.CompareExchange(preparing, desired, sl::AcqRel))
        {} //no-op, thread will see the new stage on it's own.
        else if (waiter.stage.CompareExchange(blocked, desired, sl::AcqRel))
            Private::WakeThread(thread);
    }

    static bool TryAcquireWaitable(WaitEntry& entry, bool inList)
    {
        auto* waitable = entry.waitable;
        auto& state = waitable->tickets;

        bool success = false;
        switch (waitable->type)
        {
        case WaitableType::Condition:
            [[fallthrough]];
        case WaitableType::Timer:
        {
            const auto tickets = state.Load(sl::Acquire);

            success = tickets == 0;
            break;
        }

        case WaitableType::Mutex:
        {
            auto tickets = state.Load(sl::Acquire);

            //TODO: atomic handoff a small amount of the time to ensure
            //the waiter queue will eventually drain and avoid a livelock.
            while (tickets != 0)
            {
                auto desired = tickets - 1;
                if (!state.CompareExchange(tickets, desired, sl::AcqRel))
                    continue;

                success = true;
                break;
            }
            break;
        }
        
        case WaitableType::SxMutex:
        {
            auto tickets = state.Load(sl::Acquire);

            while (true)
            {
                SxMutexState sxState;
                sl::MemCopy(&sxState, &tickets, sizeof(sxState));

                if (entry.isExclusive)
                {
                    if (sxState.heldExclusive == 1)
                        break;
                    if (sxState.sharedHolders != 0)
                        break;

                    sxState.heldExclusive = 1;
                }
                else
                {
                    if (sxState.heldExclusive == 1)
                        break;
                    if (!inList && sxState.exclusiveWaiters != 0)
                        break;

                    sxState.sharedHolders += 1;
                }

                decltype(tickets) desired;
                sl::MemCopy(&desired, &sxState, sizeof(desired));

                if (!state.CompareExchange(tickets, desired, sl::AcqRel))
                    continue;

                success = true;
                break;
            }
            break;
        }
        }

        if (!success)
            return false;

        entry.satisfied = true;

        return true;
    }

    NpkStatus WaitMany(sl::Span<Waitable*> what, WaitEntry* entries, 
        sl::TimeCount timeout, sl::StringSpan reason)
    {
        //TODO: any other pre-checks that thread is safe to wait
        if (CurrentIpl() != Ipl::Passive)
            return NpkStatus::NotAvailable;
        if (entries == nullptr && !what.Empty())
            return NpkStatus::InvalidArg;

        auto WaitStageToStatus = [](WaitStage stage) -> NpkStatus
        {
            switch (stage)
            {
            case WaitStage::Timedout:
                return NpkStatus::Timeout;
            case WaitStage::Reset:
                return NpkStatus::Reset;
            case WaitStage::Cancelled:
                return NpkStatus::Aborted;
            case WaitStage::Satisfied:
                return NpkStatus::Success;
            default:
                return NpkStatus::InternalError;
            }
        };

        auto* thread = GetCurrentThread();
        auto& waiter = thread->waiting;

        waiter.stage.Store(WaitStage::Preparing, sl::Release);
        waiter.lock.Lock();
        waiter.reason = reason;
        waiter.lock.Unlock();

        //1. initial setup, link the curent thread to wait entries, and the
        //wait entries to their associated waitables. Try get early satisfaction
        //if possible.
        bool satisfied = false;
        for (size_t i = 0; i < what.Size(); i++)
        {
            auto& entry = entries[i];
            auto* waitable = what[i];

            entry.inList = false;
            entry.thread = thread;
            entry.satisfied = false;
            entry.waitable = waitable;
            //NOTE: entry.isExclusive is left with the caller's value

            //try an eager acquire of the waitable now.
            if (TryAcquireWaitable(entry, false))
            {
                waiter.stage.Store(WaitStage::Satisfied, sl::Release);
                satisfied = true;
                break;
            }

            if (waitable->type == WaitableType::SxMutex && entry.isExclusive)
                AdjustExclusiveCount(waitable, true);

            waitable->listLock.Lock();
            entry.inList = true;
            waitable->waitersList.PushBack(&entry);

            //there's a TOCTOU window between when we tried to acquire eagerly
            //earlier and adding this thread to the waitable's queue.
            //My fix is to just check again while holding the list lock.
            if (TryAcquireWaitable(entry, true))
            {
                waitable->waitersList.Remove(&entry);
                entry.inList = false;
                waitable->listLock.Unlock();

                if (what[i]->type == WaitableType::SxMutex && entry.isExclusive)
                    AdjustExclusiveCount(waitable, false);
                waiter.stage.Store(WaitStage::Satisfied, sl::Release);
                satisfied = true;

                break;
            }
            waitable->listLock.Unlock();

        }

        Dpc wakeDpc {};
        wakeDpc.arg = thread;
        wakeDpc.function = WakeThreadDpc;
        waiter.wakeDpc = &wakeDpc;

        Dpc timeoutDpc {};
        timeoutDpc.arg = thread;
        timeoutDpc.function = WaitTimeoutDpc;

        ClockEvent timeoutEvent {};
        timeoutEvent.dpc = &timeoutDpc;
        timeoutEvent.expiry = GetMonotonicTime() + timeout;
        if (timeout.ticks != 0)
            AddClockEvent(&timeoutEvent);

        //2. main waiting loop.
        auto result = satisfied ? NpkStatus::Success : NpkStatus::Timeout;
        while (!satisfied && timeout.ticks != 0)
        {
            RaiseIpl(Ipl::Dpc);

            auto expected = WaitStage::Preparing;
            auto desired = WaitStage::Blocked;
            if (!waiter.stage.CompareExchange(expected, desired, sl::AcqRel))
            {
                //someone else moved us to a terminal wait stage, abort the
                //wait and report that to the caller.
                result = WaitStageToStatus(expected);
                LowerIpl(Ipl::Passive);
                break;
            }

            Private::BeginWait();
            while (true)
            {
                LowerIpl(Ipl::Passive); //allow preemption to take place.
                RaiseIpl(Ipl::Dpc); //we're back! Let's see why we woke up.

                const auto stage = waiter.stage.Load(sl::Acquire);
                if (stage != WaitStage::Blocked)
                {
                    result = WaitStageToStatus(stage);
                    break;
                }

                for (size_t i = 0; i < what.Size(); i++)
                {
                    auto& entry = entries[i];

                    if (!TryAcquireWaitable(entry, true))
                        continue;

                    waiter.stage.Store(WaitStage::Satisfied, sl::Release);

                    what[i]->listLock.Lock();
                    if (entry.inList)
                    {
                        what[i]->waitersList.Remove(&entry);
                        entry.inList = false;
                    }
                    what[i]->listLock.Unlock();

                    if (what[i]->type == WaitableType::SxMutex 
                        && entry.isExclusive)
                        AdjustExclusiveCount(what[i], false);

                    result = NpkStatus::Success;
                    break;
                }
                if (result == NpkStatus::Success)
                    break;

                //re-arm for another sleep
                Private::BeginWait();
            }

            Private::EndWait();
            LowerIpl(Ipl::Passive);
            break;
        }
        
        //3. cleanup. We're done here (for whatever result), undo any
        //linkages made in the setup phase.
        for (size_t i = 0; i < what.Size(); i++)
        {
            auto& entry = entries[i];
            
            if (!entry.satisfied && entry.inList)
            {
                auto* waitable = entry.waitable;

                waitable->listLock.Lock();
                waitable->waitersList.Remove(&entry);
                entry.inList = false;
                if (what[i]->type == WaitableType::SxMutex && entry.isExclusive)
                    AdjustExclusiveCount(waitable, false);
                waitable->listLock.Unlock();

                //TODO: optimize when this is called, no harm in doing it
                //spuriously but its a waste of cpu resources.
                QueueWaitable(waitable);
            }

            //not necessary but prevents misuse elsewhere.
            entry.thread = nullptr;
            entry.waitable = nullptr;
        }

        if (timeout.ticks != 0)
        {
            if (!RemoveClockEvent(&timeoutEvent))
                SpinUntilDpcCompleted(&timeoutDpc);
        }

        waiter.wakeDpc = nullptr;

        waiter.lock.Lock();
        waiter.reason = {};
        waiter.lock.Unlock();

        return result;
    }

    NpkStatus WaitOne(Waitable* what, WaitEntry* entry, sl::TimeCount timeout,
        sl::StringSpan reason)
    {
        return WaitMany({ &what, 1 }, entry, timeout, reason);
    }

    static NpkStatus ResetWaitable(Waitable* what, WaitableType newType, 
        size_t tickets)
    {
        if (CurrentIpl() != Ipl::Passive)
            return NpkStatus::NotAvailable;
        if (what == nullptr)
            return NpkStatus::InvalidArg;

        switch (what->type)
        {
        case WaitableType::Condition:
            //no-op, can always be reset.
            break;

        case WaitableType::Timer:
            if (!RemoveClockEvent(&what->clockEvent))
                SpinUntilDpcCompleted(what->clockEvent.dpc);
            break;

        case WaitableType::Mutex:
            if (what->tickets.Load(sl::Acquire) == 0)
                return NpkStatus::Busy;
            break;

        case WaitableType::SxMutex:
        {
            const auto tickets = what->tickets.Load(sl::Acquire);
            SxMutexState sxState;
            sl::MemCopy(&sxState, &tickets, sizeof(sxState));

            if (sxState.heldExclusive || sxState.sharedHolders != 0)
                return NpkStatus::Busy;
            break;
        }
        }

        what->listLock.Lock();
        while (!what->waitersList.Empty())
        {
            auto* entry = what->waitersList.PopFront();
            entry->inList = false;

            auto& stage = entry->thread->waiting.stage;
            auto preparing = WaitStage::Preparing;
            auto blocked = WaitStage::Blocked;
            auto desired = WaitStage::Reset;

            if (stage.CompareExchange(preparing, desired, sl::AcqRel))
            {} //no-op, thread will detect the new state and error out
            else if (stage.CompareExchange(blocked, desired, sl::AcqRel))
                Private::WakeThread(entry->thread);
        }

        //no one is waiting or preparing to wait on the waitable anymore,
        //so we reset its state before releasing the list lock.
        //The lock ensures no one else tries to change it's type, and
        //therefore the behaviour of the other wait functions.
        what->type = newType;
        what->owner = nullptr;
        switch (what->type)
        {
        case WaitableType::Condition:
            what->tickets.Store(tickets, sl::Release);
            break;

        case WaitableType::Timer:
            what->tickets.Store(1, sl::Release);
            break;

        case WaitableType::Mutex:
            what->tickets.Store(tickets, sl::Release);
            break;

        case WaitableType::SxMutex:
            what->tickets.Store(0, sl::Release);
            break;
        }
        what->listLock.Unlock();

        return NpkStatus::Success;
    }

    NpkStatus ResetCondition(Condition* what, size_t tickets)
    {
        auto result = ResetWaitable(what, WaitableType::Condition, tickets);

        return result;
    }

    NpkStatus ResetTimer(Timer* what, sl::TimePoint expiry, Dpc* dpc)
    {
        auto result = ResetWaitable(what, WaitableType::Timer, 1);
        if (result != NpkStatus::Success)
            return result;

        what->clockEvent.waitable = what;
        what->clockEvent.expiry = expiry;
        what->clockEvent.dpc = dpc;

        return NpkStatus::Success;
    }

    NpkStatus ResetMutex(Mutex* what, size_t tickets)
    {
        if (tickets > 1)
            return NpkStatus::Unsupported; //TODO: Support!

        auto result= ResetWaitable(what, WaitableType::Mutex, tickets);

        return result;
    }

    NpkStatus ResetSxMutex(SxMutex* what)
    {
        auto result= ResetWaitable(what, WaitableType::SxMutex, 0);

        return result;
    }

    void SetCondition(Condition* what, size_t count)
    {
        if (what == nullptr)
            return;
        if (what->type != WaitableType::Condition)
            return;

        const auto prev = what->tickets.FetchSub(count, sl::AcqRel);
        if (prev != count)
            return;

        QueueWaitable(what);
    }

    void Private::SignalTimerWaitable(Timer* timer)
    {
        if (timer == nullptr)
            return;
        if (timer->type != WaitableType::Timer)
            return;

        const auto prev = timer->tickets.Exchange(0);
        if (prev == 0)
            return;

        QueueWaitable(timer);
    }

    void SetTimer(Timer* timer, sl::Opt<sl::TimePoint> expiry)
    {
        if (timer == nullptr)
            return;
        if (timer->type != WaitableType::Timer)
            return;
        if (timer->tickets.Load(sl::Acquire) != 1)
            return;
        
        if (expiry.HasValue())
            timer->clockEvent.expiry = *expiry;

        AddClockEvent(&timer->clockEvent);
    }

    NpkStatus AcquireMutex(Mutex* mutex, sl::TimeCount timeout,
        sl::StringSpan reason)
    {
        if (mutex == nullptr)
            return NpkStatus::InvalidArg;
        if (mutex->type != WaitableType::Mutex)
            return NpkStatus::InvalidArg;

        WaitEntry entry {};
        auto result = WaitOne(mutex, &entry, timeout, reason);
        if (result != NpkStatus::Success)
            return result;

        mutex->owner = GetCurrentThread();

        return result;
    }

    void ReleaseMutex(Mutex* mutex)
    {
        if (mutex == nullptr)
            return;
        if (mutex->type != WaitableType::Mutex)
            return;

        mutex->owner = nullptr;
        mutex->tickets.Add(1, sl::Release);
        QueueWaitable(mutex);
    }

    NpkStatus AcquireSxMutexShared(SxMutex* mutex, sl::TimeCount timeout,
        sl::StringSpan reason)
    {
        if (mutex == nullptr)
            return NpkStatus::InvalidArg;
        if (mutex->type != WaitableType::SxMutex)
            return NpkStatus::InvalidArg;

        WaitEntry entry {};
        entry.isExclusive = false;

        auto result = WaitOne(mutex, &entry, timeout, reason);
        if (result != NpkStatus::Success)
            return result;
        //TODO: set `mutex->owner` to a list of all shared holders?

        return result;
    }

    NpkStatus AcquireSxMutexExclusive(SxMutex* mutex, sl::TimeCount timeout,
        sl::StringSpan reason)
    {
        if (mutex == nullptr)
            return NpkStatus::InvalidArg;
        if (mutex->type != WaitableType::SxMutex)
            return NpkStatus::InvalidArg;

        WaitEntry entry {};
        entry.isExclusive = true;

        auto result = WaitOne(mutex, &entry, timeout, reason);
        if (result != NpkStatus::Success)
            return result;

        mutex->owner = GetCurrentThread();

        return result;
    }

    void ReleaseSxMutexShared(SxMutex* mutex)
    {
        if (mutex == nullptr)
            return;
        if (mutex->type != WaitableType::SxMutex)
            return;

        auto tickets = mutex->tickets.Load(sl::Acquire);
        while (true)
        {
            SxMutexState sxState;
            sl::MemCopy(&sxState, &tickets, sizeof(sxState));
            sxState.sharedHolders--;

            decltype(tickets) desired;
            sl::MemCopy(&desired, &sxState, sizeof(desired));

            if (!mutex->tickets.CompareExchange(tickets, desired, sl::AcqRel))
                continue;

            if (sxState.sharedHolders == 0 && !sxState.heldExclusive)
                QueueWaitable(mutex);
            break;
        }
    }

    void ReleaseSxMutexExclusive(SxMutex* mutex)
    {
        if (mutex == nullptr)
            return;
        if (mutex->type != WaitableType::SxMutex)
            return;

        mutex->owner = nullptr;

        auto tickets = mutex->tickets.Load(sl::Acquire);
        while (true)
        {
            SxMutexState sxState;
            sl::MemCopy(&sxState, &tickets, sizeof(sxState));
            sxState.heldExclusive = 0;

            decltype(tickets) desired;
            sl::MemCopy(&desired, &sxState, sizeof(desired));

            if (!mutex->tickets.CompareExchange(tickets, desired, sl::AcqRel))
                continue;

            break;
        }

        QueueWaitable(mutex);
    }
}
