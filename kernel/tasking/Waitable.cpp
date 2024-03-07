#include <tasking/Waitable.h>
#include <tasking/Scheduler.h>

namespace Npk::Tasking
{
    void Waitable::Wait()
    {
        /*
        const size_t ourId = Thread::Current().Id();

        Scheduler::Global().Suspend(true);
        lock.Lock();
        if (pendingTriggers > 0)
        {
            pendingTriggers--;
            lock.Unlock();
            Scheduler::Global().Suspend(false);
            return;
        }

        waitingThreads.EmplaceBack(ourId);
        Scheduler::Global().DequeueThread(ourId);
        lock.Unlock();
        Scheduler::Global().Suspend(false); //restore scheduling before yielding

        Scheduler::Global().Yield();
        */
    }

    bool Waitable::WouldWait()
    {
        return pendingTriggers > 0;
    }

    void Waitable::Trigger(bool accumulate, size_t count)
    {
        /*
        sl::ScopedLock scopeLock(lock);
        
        if (waitingThreads.Empty())
        {
            if (accumulate)
                pendingTriggers += count;
            return;
        }

        ScheduleGuard schedGuard;
        while (count > 0 && waitingThreads.Size() > 0)
        {
            //wake as many threads as we should
            Scheduler::Global().EnqueueThread(waitingThreads.PopBack());
            if (accumulate)
                pendingTriggers--;
            count--;
        }

        if (accumulate)
            pendingTriggers += count;
            */
    }
}
