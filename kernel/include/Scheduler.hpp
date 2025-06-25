#pragma once

#include <KernelApi.hpp>

namespace Npk
{
    struct Scheduler;

    struct ThreadContext
    {
        IntrSpinLock lock;
        sl::ListHook queueHook;
        ArchThreadContext* context;
        Scheduler* sched;
        uint8_t basePriority;
        uint8_t priorityBoost;

        IplSpinLock<Ipl::Dpc> waitEntriesLock;
        sl::Span<WaitEntry> waitEntries;
        sl::StringSpan waitReason;

        size_t Priority()
        {
            return basePriority + priorityBoost;
        }
    };

    using RunQueue = sl::List<ThreadContext, &ThreadContext::queueHook>;

    void Yield(bool voluntary);
    void EnqueueThread(ThreadContext* thread);
    void DequeueThread(ThreadContext* thread);
    void SetIdleThread(ThreadContext* thread);
    void OnPassiveRunLevel();

    //TODO: BoostThread(), SetThreadPriority()
}
