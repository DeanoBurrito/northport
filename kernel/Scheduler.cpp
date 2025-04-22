#include <Scheduler.hpp>
#include <Maths.h>

namespace Npk
{
    struct Scheduler
    {
    private:
        sl::SpinLock queuesLock;
        sl::Span<RunQueue> queues;

    public:
        ThreadContext* idleThread;
        bool switchPending;

        void PushThread(ThreadContext* thread)
        {
            sl::ScopedLock lock(queuesLock);
            const size_t priority = sl::Min(thread->EffectivePriority(), queues.Size() - 1);
            queues[priority].PushBack(thread);
        }

        ThreadContext* PopThread()
        {
            sl::ScopedLock lock(queuesLock);

            for (int i = queues.Size() - 1; i >= 0; i--)
            {
                if (queues.Empty())
                    continue;

                return queues[i].PopFront();
            }

            return idleThread;
        }

        void RemoveThread(ThreadContext* thread)
        {
            sl::ScopedLock lock(queuesLock);
            const size_t priority = sl::Min(thread->EffectivePriority(), queues.Size() - 1);
            queues[priority].Remove(thread);
        }
    };

    CPU_LOCAL(Scheduler, localScheduler);

    void Yield()
    {
        const bool prevIntrs = IntrsOff();
        localScheduler->switchPending = false;

        ThreadContext* current = GetCurrentThread();
        ThreadContext* next = localScheduler->PopThread();
        if (current == next)
        {
            //this is only allowed if we're the idle thread, otherwise
            //the current thread shouldn't be returned from PopThread().
            NPK_ASSERT(current == localScheduler->idleThread);

            if (prevIntrs)
                IntrsOn();
            return;
        }

        SetCurrentThread(next);
        ArchSwitchThread(&current->context, next->context);

        if (prevIntrs)
            IntrsOn();
    }

    void EnqueueThread(ThreadContext* thread, size_t boost)
    {
        NPK_ASSERT(thread != nullptr);

        sl::ScopedLock threadLock(thread->lock);
        NPK_ASSERT(thread->sched == nullptr);
        thread->sched = &*localScheduler;
        thread->priorityBoost = boost;

        localScheduler->PushThread(thread);
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
        thread->priorityBoost = 0;
    }

    void SetIdleThread(ThreadContext* thread)
    {
        localScheduler->idleThread = thread;
        thread->priority = 0;
    }

    void OnPassiveRunLevel()
    {
        if (localScheduler->switchPending)
            Yield();
    }
}
