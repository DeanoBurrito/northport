#include <KernelApi.hpp>
#include <Scheduler.hpp>
#include <Maths.h>

/* Big theory comment:
 * The wait subsysem is one of the higher level parts of the kernel core. It operates almost
 * entirely at Ipl::Dpc, and consists of three main entities:
 * - waitables, the the things we can wait-on/signal.
 * - the thread that does the waiting.
 * - wait entries, when in-use an array of these is associated with the waiting thread, and each
 *   entry links to a single waitable.
 *
 * Locking is a little tricky here, since waiting and signalling essentially happen
 * in opposite directions. I've tried to shift as much of the work as possible to the
 * waiter/thread side, which means signalling only needs to lock the waitable and atomically
 * update the associated wait entry status.
 */
namespace Npk
{
    static void LockWaitables(sl::Span<WaitEntry> entries)
    {
        AssertIpl(Ipl::Dpc);

        if (entries.Size() == 1)
        {
            entries[0].waitable->lock.Lock();
            return;
        }

        /* Locking waitables is a bit painful, since the caller can place the waitables
         * into the span in any order, meaning one thread might wait(a + b) while another
         * does wait(b +a). If we naively try to lock by the order they appear in the span we'll
         * deadlock eventually.
         * I cant remember where I originally saw this solution (if you know, I'd love to add credit
         * here), what we do is lock based on the memory address of each waitable, starting at
         * the lowest. That way all calls to wait() take these locks in the same order.
         */
        size_t lockedCount = 0;
        uintptr_t lastAddr = 0;
        while (lockedCount < entries.Size())
        {
            size_t candidate = 0;
            uintptr_t candidateAddr = static_cast<uintptr_t>(~0);
            for (size_t i = 0; i < entries.Size(); i++)
            {
                const uintptr_t addr = reinterpret_cast<uintptr_t>(entries[i].waitable);
                if (addr <= lastAddr)
                    continue;
                if (addr > candidateAddr)
                    continue;

                candidateAddr = addr;
                candidate = i;
            }

            NPK_ASSERT(candidateAddr != static_cast<uintptr_t>(~0));
            entries[candidate].waitable->lock.Lock();
            lastAddr = candidateAddr;
            lockedCount++;
        }
    }

    static void UnlockWaitables(sl::Span<WaitEntry> entries)
    {
        AssertIpl(Ipl::Dpc);

        //we dont need to be careful about unlocking order here,
        //just iterate through them all.
        for (size_t i = 0; i < entries.Size(); i++)
            entries[i].waitable->lock.Unlock();
    }

    //NOTE: assumes entry->waitable is locked
    static bool TryAcquireWaitable(WaitEntry* entry)
    {
        if (entry->waitable->tickets == 0)
            return false;

        entry->waitable->waiters.Remove(entry);

        //perform any processing the waitable requires
        switch (entry->waitable->type)
        {
        case WaitableType::Condition: break; //conditions and timers must be cleared manually
        case WaitableType::Timer: break;
        case WaitableType::Mutex:
            entry->waitable->tickets = 0;
            entry->waitable->mutexHolder = entry->thread;
            break;
        }

        return true;
    }

    //NOTE: assumes waitable is locked, returns number of possible acquisitions
    static size_t SetWaitableSignalled(Waitable* what)
    {
        switch (what->type)
        {
        case WaitableType::Condition:
        case WaitableType::Timer:
            what->tickets = 1;
            return static_cast<size_t>(~0);
        case WaitableType::Mutex:
            what->tickets++;
            return what->tickets;
        }
    }

    //NOTE: assumes thread->waitEntries->waitables are all locked
    static WaitStatus TryCompleteWait(ThreadContext* thread)
    {
        WaitStatus result = WaitStatus::Success;

        for (size_t i = 0; i < thread->waitEntries.Size(); i++)
        {
            auto entry = &thread->waitEntries[i];

            if (entry->status.Load(sl::Acquire) == WaitStatus::Success)
            {
                if (!TryAcquireWaitable(entry))
                {
                    entry->waitable->waiters.PushFront(entry);
                    entry->status.Store(WaitStatus::Incomplete, sl::Release);
                }
            }

            const auto status = (unsigned)thread->waitEntries[i].status.Load(sl::Acquire);
            result = (WaitStatus)sl::Max((unsigned)result, status);
        }

        return result;
    }

    static void StopWait(ThreadContext* thread, WaitStatus why)
    {
        thread->waitEntriesLock.Lock();
        if (thread->waitEntries.Empty())
        {
            //we dont requeue the thread here, since it should already be running if its wait
            //entries are empty.
            thread->waitEntriesLock.Unlock();
            return;
        }

        LockWaitables(thread->waitEntries);
        for (size_t i = 0; i < thread->waitEntries.Size(); i++)
            thread->waitEntries[i].status.Store(why, sl::Release);
        UnlockWaitables(thread->waitEntries);
        thread->waitEntriesLock.Unlock();

        EnqueueThread(thread);
    }

    static void DoTimeoutWait(Dpc* dpc, void* arg)
    {
        (void)dpc;

        auto thread = static_cast<ThreadContext*>(arg);
        StopWait(thread, WaitStatus::Timedout);
    }

    void CancelWait(ThreadContext* thread)
    {
        NPK_CHECK(thread != nullptr, );
        StopWait(thread, WaitStatus::Cancelled);
    }

    WaitStatus WaitMany(sl::Span<Waitable*> what, WaitEntry* entries, sl::TimeCount timeout, sl::StringSpan reason)
    {
        auto ResetThreadWaitState = [=](ThreadContext* thread)
        {
            UnlockWaitables(thread->waitEntries);
            thread->waitEntries = {};
            thread->waitReason = {};
            thread->waitEntriesLock.Unlock();
        };

        if (what.Empty())
            return WaitStatus::Success;

        NPK_CHECK(entries != nullptr, WaitStatus::Incomplete);
        AssertIpl(Ipl::Passive);

        //0. initial setup, set wait entries to target waitables, associate wait entries and thread.
        auto thread = GetCurrentThread();
        for (size_t i = 0; i < what.Size(); i++)
        {
            auto& e = entries[i];
            e.waitable = what[i];
            e.thread = thread;
            e.status = WaitStatus::Incomplete;
        }

        thread->waitEntriesLock.Lock();
        thread->waitEntries = { entries, what.Size() };
        thread->waitReason = reason;
        LockWaitables(thread->waitEntries);

        //1. try to complete the wait now, before we've queued ourselves 
        if (TryCompleteWait(thread) == WaitStatus::Success)
        {
            ResetThreadWaitState(thread);
            return WaitStatus::Success;
        }
        
        //2. couldn't complete the wait right away, if the timeout is 0 - it was only a query, 
        //so we return now.
        if (timeout.ticks == 0)
        {
            ResetThreadWaitState(thread);
            return WaitStatus::Timedout;
        }

        Dpc timeoutDpc;
        timeoutDpc.arg = thread;
        timeoutDpc.function = DoTimeoutWait;

        ClockEvent timeoutEvent {};
        timeoutEvent.dpc = &timeoutDpc;
        timeoutEvent.expiry = PlatReadTimestamp() + timeout;
        AddClockEvent(&timeoutEvent);

        //3. prepare for wakeup: add wait entries to the queue of each waitable
        for (size_t i = 0; i < thread->waitEntries.Size(); i++)
        {
            auto* entry = &thread->waitEntries[i];
            entry->waitable->waiters.PushBack(entry);
        }

        WaitStatus result;
        while (true)
        {
            //4. prepare and enact sleeping. Once the final lock is dropped here, ipl is lowered to
            //passive and the pending rescheduler triggered by DequeueThread(self) happens.
            DequeueThread(thread);
            UnlockWaitables(thread->waitEntries);
            thread->waitEntriesLock.Unlock(); //ipl is lowered here; reschedule is triggered

            //5. we've woken up: see what happened and if we can complete any of the waits.
            //Note that we dont do atomic handoff to avoid 'lock convoys' (thanks will/hyenasky for 
            //talking about this). The problem that can occur is a thread that is handed the mutex 
            //will be scheduled but may not run for a while, in the meantime an already-running thread
            //may want to acquire the same mutex, and end up dequeued and end up waiting when it could
            //have completed it's work sooner.
            //The solution is to wake the first waiter *without* handing them the mutex. If they fail
            //to acquire it, they go back at the front of the queue. That means any awoken waiters (us)
            //must try to acquire the mutex or go back to sleep.
            result = WaitStatus::Incomplete;

            thread->waitEntriesLock.Lock();
            LockWaitables(thread->waitEntries);

            result = TryCompleteWait(thread);
            if (result != WaitStatus::Incomplete)
                break;
            //else: spurious wakeup or someone else snatched the mutex before we could - back to sleep!
        }

        //6. cleanup: successful wait entries are removed from their waitable's queue, we need to remove
        //the other wait entries though.
        for (size_t i = 0; i < thread->waitEntries.Size(); i++)
        {
            auto entry = &thread->waitEntries[i];
            if (entry->status != WaitStatus::Success)
                entry->waitable->waiters.Remove(entry);
        }

        ResetThreadWaitState(thread);
        return result;
    }
    static_assert(static_cast<unsigned>(WaitStatus::Timedout) > static_cast<unsigned>(WaitStatus::Incomplete));
    static_assert(static_cast<unsigned>(WaitStatus::Reset) > static_cast<unsigned>(WaitStatus::Timedout));
    static_assert(static_cast<unsigned>(WaitStatus::Cancelled) > static_cast<unsigned>(WaitStatus::Reset));
    static_assert(static_cast<unsigned>(WaitStatus::Success) > static_cast<unsigned>(WaitStatus::Cancelled));

    void SignalWaitable(Waitable* what)
    {
        NPK_CHECK(what != nullptr, );

        //TODO: support for signalling from IPLs > DPC (we'll need this for devices)
        //we could have a hack for >DPC runlevels where the signalling is added to a queue to a kernel provided dpc, and which then runs.
        //From the caller's perspective this would do the job. It'd really just be a convinience thing.
        NPK_ASSERT(CurrentIpl() == Ipl::Passive || CurrentIpl() == Ipl::Dpc);

        what->lock.Lock();
        const size_t wakeCount = SetWaitableSignalled(what);

        for (size_t i = 0; i < wakeCount; i++)
        {
            auto waiter = what->waiters.PopFront();
            if (waiter == nullptr)
                break;

            waiter->status.Store(WaitStatus::Success, sl::Release);
            EnqueueThread(waiter->thread);
        }
        what->lock.Unlock();
        //TODO: we'll need figure out early completion, will need a per-thread atomic wait status
    }

    void ResetWaitable(Waitable* what, WaitableType newType)
    {
        NPK_CHECK(what != nullptr, );

        bool canReset = false;
        while (!canReset)
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
            }

            if (canReset)
                break;
            what->lock.Unlock(); //TODO: this could lead to starvation, we should use a fair lock
        }
        
        while (true)
        {
            auto waiter = what->waiters.PopFront();
            waiter->status.Store(WaitStatus::Reset, sl::Release);
            EnqueueThread(waiter->thread);
        }

        what->tickets = 0;
        what->type = newType;
        what->lock.Unlock();
    }
}
