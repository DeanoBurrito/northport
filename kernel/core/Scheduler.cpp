#include <Scheduler.hpp>
#include <Maths.h>

/* Scheduler thoughts:
 * - dynamic priority inheritence is a must. We need to support modifying the priority of
 *   live threads (running or readied). Naturally if a readied thread is boosted to a higher priotity
 *   it shoul pre-empt the current one.
 * - we can support modifying priorities easily:
 *   - the current thread doesnt need nay actions taken, since its already running. Unless
 *   its priority is lowered, in which we switch away if it becomes too low.
 *   - readied threads need to be dequeued and requeued at the current effective priority.
 *   - all other threads are not relevant.
 */
namespace Npk
{
    struct Scheduler
    {
    private:
        IntrSpinLock queuesLock;
        sl::Span<RunQueue> queues;

    public:
        ThreadContext* idleThread;
        bool switchPending;

        void PushThread(ThreadContext* thread)
        {
            sl::ScopedLock lock(queuesLock);
            const size_t priority = sl::Min(thread->Priority(), queues.Size() - 1);
            queues[priority].PushBack(thread);
        }

        //NOTE: assumes queuesLock is held
        ThreadContext* PeekThreadLocked(int& queueIndex)
        {
            for (int i = queues.Size() - 1; i >= 0; i--)
            {
                if (queues.Empty())
                    continue;

                queueIndex = i;
                return &queues[i].Front();
            }

            return nullptr;
        }

        ThreadContext* PopThread()
        {
            sl::ScopedLock lock(queuesLock);

            int index = 0;
            auto next = PeekThreadLocked(index);
            if (next != nullptr)
                queues[index].PopFront();

            return next;
        }

        ThreadContext* PeekThread()
        {
            sl::ScopedLock lock(queuesLock);
            int ignored = 0;
            return PeekThreadLocked(ignored);
        }

        void RemoveThread(ThreadContext* thread)
        {
            sl::ScopedLock lock(queuesLock);
            const size_t priority = sl::Min(thread->Priority(), queues.Size() - 1);
            queues[priority].Remove(thread);
        }
    };

    CPU_LOCAL(Scheduler, localScheduler);

    void Yield(bool voluntary)
    {
        const bool prevIntrs = IntrsOff();
        localScheduler->switchPending = false;

        ThreadContext* current = GetCurrentThread();
        if (voluntary)
        {
            current->lock.Lock();
            current->priorityBoost = 0;
            current->lock.Unlock();
        }

        ThreadContext* next = localScheduler->PopThread();
        if (next == nullptr)
            next = localScheduler->idleThread;
        localScheduler->PushThread(current);

        SetCurrentThread(next);
        if (next != current)
            ArchSwitchThread(&current->context, next->context);

        if (prevIntrs)
            IntrsOn();
    }

    void EnqueueThread(ThreadContext* thread, uint8_t boost)
    {
        NPK_ASSERT(thread != nullptr);

        thread->lock.Lock();
        NPK_ASSERT(thread->sched == nullptr);

        thread->sched = &*localScheduler;
        thread->priorityBoost = boost;
        thread->lock.Unlock();

        localScheduler->PushThread(thread);

        if (thread->Priority() > GetCurrentThread()->Priority())
        {
            if (CurrentIpl() == Ipl::Passive)
                Yield(false);
            else
                localScheduler->switchPending = true;
        }
    }

    void DequeueThread(ThreadContext* thread)
    {
        NPK_ASSERT(thread != nullptr);

        sl::ScopedLock threadLock(thread->lock);
        NPK_ASSERT(thread->sched != nullptr);

        if (thread->sched != &*localScheduler)
        {
            NPK_UNREACHABLE();
            //TODO: IPI sync-exec function, then remote dequeue
        }

        localScheduler->RemoveThread(thread);
        thread->sched = nullptr;

        if (thread != GetCurrentThread())
            return;
        if (localScheduler->PeekThread()->Priority() > thread->Priority())
        {
            if (CurrentIpl() == Ipl::Passive)
                Yield(false);
            else
                localScheduler->switchPending = true;
        }
    }

    void SetIdleThread(ThreadContext* thread)
    {
        localScheduler->idleThread = thread;
        Log("Set idle thread to %p", LogLevel::Trace, thread);
    }

    void OnPassiveRunLevel()
    {
        if (localScheduler->switchPending)
            Yield(false);
    }
}
