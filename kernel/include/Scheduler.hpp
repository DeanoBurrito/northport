#pragma once

#include <KernelApi.hpp>

namespace Npk
{
    struct Scheduler;

    struct ThreadContext
    {
        IntrSpinLock lock;
        ArchThreadContext* context;
        Scheduler* sched;
        size_t priority;
        size_t priorityBoost;
        sl::ListHook queueHook;

        inline size_t EffectivePriority()
        {
            return priority + priorityBoost;
        }
    };

    using RunQueue = sl::List<ThreadContext, &ThreadContext::queueHook>;

    void Yield(bool voluntary);
    void EnqueueThread(ThreadContext* thread, size_t boost);
    void DequeueThread(ThreadContext* thread);
    void SetIdleThread(ThreadContext* thread);
    void OnPassiveRunLevel();
}
