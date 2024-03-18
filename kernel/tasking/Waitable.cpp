#include <tasking/Waitable.h>
#include <tasking/Scheduler.h>
#include <debug/Log.h>

namespace Npk::Tasking
{
    size_t Waitable::Signal(size_t count)
    { return WaitManager::Signal(this, count); }

    bool WaitManager::TryFinish(sl::Span<WaitEntry> entries, bool waitAll)
    {
        for (size_t i = 0; i < entries.Size(); i++)
        {
            if (entries[i].event->signalCount == 0)
            {
                if (waitAll)
                    return false;
                continue;
            }

            if (waitAll)
                continue;
            entries[i].event->signalCount--;
            entries[i].signalled = true;
            return true;
        }

        if (!waitAll)
            return false;

        for (size_t i = 0; i < entries.Size(); i++)
        {
            entries[i].event->signalCount--;
            entries[i].signalled = true;
        }

        return true;
    }

    void WaitManager::LockAll(sl::Span<WaitEntry> entries)
    {
        for (size_t i = 0; i < entries.Size(); i++)
            entries[i].event->lock.Lock();
    }

    void WaitManager::UnlockAll(sl::Span<WaitEntry> entries)
    {
        for (size_t i = 0; i < entries.Size(); i++)
            entries[i].event->lock.Unlock();
    }

    size_t WaitManager::WaitMany(sl::Span<Waitable> events, sl::Span<WaitEntry> entries, sl::ScaledTime timeout, bool waitAll)
    {
        VALIDATE_(events.Size() == entries.Size(), 0);
        VALIDATE_(timeout.units == 0 || timeout.units == -1, 0); //TODO: timeouts

        //some initial setup
        const auto prevRunlevel = EnsureRunLevel(RunLevel::Dpc);
        for (size_t i = 0; i < entries.Size(); i++)
        {
            entries[i].next = nullptr;
            entries[i].event = &events[i];
            entries[i].thread = static_cast<Thread*>(CoreLocal()[LocalPtr::Thread]);
            entries[i].cohort = entries;
            entries[i].signalled = false;
            entries[i].waitAll = waitAll;
        }

        LockAll(entries);

        //see if we can return immediately, or otherwise return because this was
        //a poll (timeout of 0).
        if (TryFinish(entries, waitAll) || timeout.units == 0)
        {
            UnlockAll(entries);
            if (prevRunlevel.HasValue())
                LowerRunLevel(*prevRunlevel);

            size_t completeCount = 0;
            for (size_t i = 0; i < entries.Size(); i++)
                completeCount += entries[i].signalled ? 1 : 0;
            return completeCount;
        }

        //add an entry to each events queue
        for (size_t i = 0; i < events.Size(); i++)
            events[i].waiters.PushBack(&entries[i]);

        Scheduler::Global().DequeueThread(static_cast<Thread*>(CoreLocal()[LocalPtr::Thread]));
        UnlockAll(entries);
        if (prevRunlevel.HasValue())
            LowerRunLevel(*prevRunlevel);
        Scheduler::Global().Yield();

        return waitAll ? entries.Size() : 1;
    }

    void WaitManager::CancelWait(Thread* thread)
    {
        ASSERT_UNREACHABLE();
    }

    size_t WaitManager::Signal(Waitable* event, size_t count)
    {
        ASSERT_(event != nullptr);

        const auto prevRunLevel = EnsureRunLevel(RunLevel::Dpc);
        event->lock.Lock();
        event->signalCount += count;
        event->lock.Unlock();

        size_t wokeCount = 0; //hmm
        for (WaitEntry* waiter = event->waiters.Begin(); waiter != nullptr; waiter = waiter->next)
        {
            LockAll(waiter->cohort);

            if (TryFinish(waiter->cohort, waiter->waitAll))
            {
                //we were able to wake the waiter, remove it from any queues, enqueue it for scheduling
                for (size_t i = 0; i < waiter->cohort.Size(); i++)
                    ASSERT_(waiter->cohort[i].event->waiters.Remove(&waiter->cohort[i]));
                Scheduler::Global().EnqueueThread(waiter->thread);
                wokeCount++;
            }
            UnlockAll(waiter->cohort);

            //optimization: exit early if this event cant wake any more waiters.
            sl::ScopedLock eventLock(event->lock);
            if (event->signalCount == 0)
                break;
        }

        if (prevRunLevel.HasValue())
            LowerRunLevel(*prevRunLevel);
        return wokeCount;
    }
}
