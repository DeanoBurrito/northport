#include <private/Core.hpp>
#include <Vm.hpp>

//TODO: dynamically spawn more worker threads when queues are not being drained quickly enough, up to some upper limit.

namespace Npk
{
    constexpr size_t WorkerMaxIdleMs = 100;

    CPU_LOCAL(ThreadContext, static primaryWorker);
    CPU_LOCAL(IplSpinLock<Ipl::Dpc>, static workItemConsumerLock);

    static void NotifyWorkItemComplete(WorkItem* item)
    {
        (void)item;
        //TODO: wake threads blocked in WaitUntilWorkItemComplete(), in the
        //blocking path.
    }

    //NOTE: this thread is pinned to a cpu core and wont ever migrate.
    void WorkItemThreadEntry(void* arg)
    {
        Log("WorkItem thread spawned %p", LogLevel::Verbose,
            GetCurrentThread());

        auto* status = static_cast<RemoteCpuStatus*>(arg);

        while (true)
        {
            workItemConsumerLock->Lock();
            auto* item = status->workItems.Pop();
            workItemConsumerLock->Unlock();

            if (item == nullptr)
            {
                SetConditionTo(&status->workItemsPending, 1);

                workItemConsumerLock->Lock();
                item = status->workItems.Pop();
                workItemConsumerLock->Unlock();

                if (item == nullptr)
                {
                    WaitEntry entry {};
                    const auto result = WaitOne(&status->workItemsPending, 
                        &entry, { WorkerMaxIdleMs, sl::Millis });

                    if (result == NpkStatus::Timeout
                        && GetCurrentThread() != &primaryWorker)
                        break;

                    continue;
                }
            }

            auto pending = WorkItemState::Pending;
            auto executing = WorkItemState::Executing;
            if (!item->state.CompareExchange(pending, executing, sl::AcqRel))
            {
                auto pendingCancel = WorkItemState::PendingCancel;
                if (item->state.CompareExchange(pendingCancel,
                    WorkItemState::Idle, sl::AcqRel))
                    NotifyWorkItemComplete(item);
                else
                {
                    Log("Dequeued work item %p in bad state %u.", 
                        LogLevel::Error, item, item->state.Load(sl::Relaxed));
                }

                continue;
            }

            item->function(item, item->arg);

            //compare-exchange back to the idle state: since we support a work
            //item re-queueing itself while running its state may have changed
            //which we dont want to clobber.
            auto idle = WorkItemState::Idle;
            if (item->state.CompareExchange(executing, idle, sl::AcqRel))
                NotifyWorkItemComplete(item);
        }

        Log("WorkItem thread despawning due to timeout %p", LogLevel::Verbose,
            GetCurrentThread());

        ExitThread(0);
    }

    void Private::InitLocalWorker() //TODO: call this late, requires VM
    {
        auto* status = RemoteStatus(MyCoreId());
        ResetCondition(&status->workItemsPending, 1);

        void* stackPtr;
        auto result = AllocKernelStack(&stackPtr);
        NPK_ASSERT(result == NpkStatus::Success);

        const auto entry = reinterpret_cast<uintptr_t>(WorkItemThreadEntry);
        const uintptr_t arg = reinterpret_cast<uintptr_t>(status);
        const auto stack = reinterpret_cast<uintptr_t>(WorkItemThreadEntry);

        ResetThread(&primaryWorker);
        result = PrepareThread(&primaryWorker, entry, arg, stack, MyCoreId());
        NPK_ASSERT(result == NpkStatus::Success);

        EnqueueThread(&primaryWorker);

        Log("Local worker thread spawned, %p", LogLevel::Info, &primaryWorker);
    }

    NpkStatus ResetWorkItem(WorkItem* item, WorkItemEntry func, void* arg)
    {
        if (item == nullptr)
            return NpkStatus::InvalidArg;
        if (func == nullptr)
            return NpkStatus::InvalidArg;

        const auto state = item->state.Load(sl::Acquire);
        if (state != WorkItemState::Idle && state != WorkItemState::Invalid)
            return NpkStatus::InUse;

        item->state = WorkItemState::Idle;
        item->function = func;
        item->arg = arg;
        item->queue = nullptr;

        return NpkStatus::Success;
    }

    NpkStatus QueueWorkItem(WorkItem* item, sl::Opt<CpuId> who)
    {
        if (item == nullptr)
            return NpkStatus::InvalidArg;
        if (item->function == nullptr)
            return NpkStatus::InvalidArg;

        CpuId target = MyCoreId();
        if (who.HasValue())
            target = *who;

        auto status = RemoteStatus(target);
        if (status == nullptr)
            return NpkStatus::InvalidArg;

        while (true)
        {
            const auto state = item->state.Load(sl::Acquire);

            if (state != WorkItemState::Idle 
                && state != WorkItemState::Executing)
                return NpkStatus::Busy;

            auto expected = state;
            if (item->state.CompareExchange(expected, WorkItemState::Pending,
                sl::AcqRel))
                break;
        }

        item->queue = status;
        status->workItems.Push(item);
        SetConditionTo(&status->workItemsPending, 0);

        return NpkStatus::Success;
    }

    NpkStatus WaitUntilWorkItemComplete(WorkItem* item, bool spin)
    {
        if (item == nullptr)
            return NpkStatus::InvalidArg;

        if (!spin)
            return NpkStatus::Unsupported; //TODO: blocking wait

        while (item->state.Load(sl::Acquire) != WorkItemState::Idle)
            sl::HintSpinloop();

        return NpkStatus::Success;
    }

    NpkStatus CancelWorkItem(WorkItem* item, bool wait, bool spin)
    {
        if (item == nullptr)
            return NpkStatus::InvalidArg;

        while (true)
        {
            auto state = item->state.Load(sl::Acquire);
            auto desired = WorkItemState::PendingCancel;

            switch (state)
            {
            case WorkItemState::Pending:
                if (item->state.CompareExchange(state, desired, sl::AcqRel))
                    break;
                continue;

            case WorkItemState::Executing:
                if (item->state.CompareExchange(state, desired, sl::AcqRel))
                {
                    item->queue->workItems.Push(item);
                    SetConditionTo(&item->queue->workItemsPending, 0);

                    break;
                }
                continue;

            case WorkItemState::PendingCancel:
                break;

            default:
                return NpkStatus::NotAvailable;
            }
        }

        if (wait)
            return WaitUntilWorkItemComplete(item, spin);

        return NpkStatus::Success;
    }
}
