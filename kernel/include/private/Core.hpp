#pragma once

#include "../Core.hpp"

/* This file (and the ::Private namespace) contains declarations only intended
 * for use by other components of the kernel core. No promises are made about
 * the effects of anything here remaining stable.
 */
namespace Npk::Private
{
    void ResetCycleAccounts(CycleAccount first);
    void SetMyNodePointer(uintptr_t addr);
    void InitLocalScheduler(ThreadContext* idle);
    void InitLocalWorker();
    void InitPageAccessCache(uintptr_t slotsBase, size_t slotsCount);

    void ClearIplPending(IplWord bits);
    void SignalPendingWaitables();
    void OnAlarmIpl();

    //returns whether scheduler accepted beginning a wait. If false means
    //the caller (wait subsystem) should re-check its state.
    bool BeginWait(sl::Span<WaitEntry> waitingOn);
    void EndWait();
    void WakeThread(ThreadContext* thread);

    void WorkItemThreadEntry(void* arg);
    void CheckPendingRcuQuiesce();

    void AcquirePanicOutputs(LogSinkList& sinks);
}
