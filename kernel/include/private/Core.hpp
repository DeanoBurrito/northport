#pragma once

#include "../Core.hpp"

/* This file (and the ::Private namespace) contains declarations only intended
 * for use by other components of the kernel core. No promises are made about
 * the effects of anything here remaining stable.
 */
namespace Npk::Private
{
    void SetMyNodePointer(uintptr_t addr);
    void InitLocalScheduler(ThreadContext* idle);
    void InitLocalWorker();
    void PrePassiveRunLevel();
    void OnPassiveRunLevel();
    void BeginWait(sl::Span<WaitEntry> waitingOn);
    void EndWait();
    void WakeThread(ThreadContext* thread);
    void WorkItemThreadEntry(void* arg);
    void SignalTimerWaitable(Timer* timer);

    void AcquirePanicOutputs(LogSinkList& sinks);
}
