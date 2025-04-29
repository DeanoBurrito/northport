#include <Scheduler.hpp>
#include <Maths.h>

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
        if (current == next)
        {
            //this is only allowed if we're the idle thread, otherwise
            //the current thread shouldn't be returned from PopThread().
            NPK_ASSERT(current == localScheduler->idleThread);

            if (prevIntrs)
                IntrsOn();
            return;
        }
        localScheduler->PushThread(current);

        SetCurrentThread(next);
        ArchSwitchThread(&current->context, next->context);

        if (prevIntrs)
            IntrsOn();
    }

    void EnqueueThread(ThreadContext* thread, size_t boost)
    {
        NPK_ASSERT(thread != nullptr);

        thread->lock.Lock();
        NPK_ASSERT(thread->sched == nullptr);

        thread->sched = &*localScheduler;
        thread->priorityBoost = boost;
        thread->lock.Unlock();

        localScheduler->PushThread(thread);

        if (thread->EffectivePriority() > GetCurrentThread()->EffectivePriority())
        {
            localScheduler->switchPending = true;

            if (CurrentIpl() == Ipl::Passive)
                Yield(false);
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
        thread->priorityBoost = 0;
        thread->sched = nullptr;
    }

    void SetIdleThread(ThreadContext* thread)
    {
        localScheduler->idleThread = thread;
        thread->priority = 0;
        Log("Set idle thread to %p", LogLevel::Trace, thread);
    }

    void OnPassiveRunLevel()
    {
        if (localScheduler->switchPending)
            Yield(false);
    }
}
