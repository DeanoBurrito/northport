#include <private/Core.hpp>

//NOTE: this is needed for EbrBarrier() to allocate the per-actor fence.
#include <Vm.hpp>

namespace Npk
{
    constexpr size_t ExpediteThreshold = 1000;
    constexpr HeapTag EbrHeapTag = NPK_MAKE_HEAP_TAG("Ebr!");
    constexpr size_t EbrWorkerIntervalMicros = 500;

    struct EbrFence
    {
        EbrItem item;
        Condition* condition;
    };
    static_assert(offsetof(EbrFence, item) == 0);

    SL_ALWAYS_INLINE
    bool EpochGe(size_t a, size_t b)
    {
        return (ptrdiff_t)(a - b) >= 0;
    }

    SL_ALWAYS_INLINE 
    bool EpochLt(size_t a, size_t b)
    {
        return (ptrdiff_t)(a - b) < 0;
    }

    //returns if an actor passed a specific epoch or was in an idle state.
    static bool ActorPassedEpoch(EbrActor& actor, size_t target)
    {
        //was the actor in an idle state at the start of the last epoch?
        if (actor.engine.idleCache & 0b1)
            return true;

        //has the actor left and re-entered an idle state during this epoch?
        if (actor.idle.Load(sl::Acquire) != actor.engine.idleCache)
            return true;

        //compare the actor's last observed epoch with the target
        if (EpochGe(actor.epoch.Load(sl::Acquire), target))
            return true;

        return false;
    }

    static void EbrDpcCallback(Dpc* dpc, void* arg)
    {
        (void)dpc;

        auto& dom = *static_cast<EbrDomain*>(arg);

        QueueWorkItem(&dom.engine.workItem, {});
    }

    static void EbrWorker(WorkItem* self, void* arg)
    {
        (void)self;

        auto& dom = *static_cast<EbrDomain*>(arg);

        if (!dom.engine.inFlight.Load(sl::Acquire))
        {
            if (dom.pending.Load(sl::Acquire) == 0)
                return;

            dom.engine.targetEpoch = dom.epoch.FetchAdd(1, sl::AcqRel) + 1;

            for (size_t i = 0; i < dom.actors.Size(); i++)
            {
                auto& actor = dom.actors[i];

                actor.engine.idleCache = actor.idle.Load(sl::Acquire);
                actor.engine.done = false;
            }

            dom.engine.inFlight.Store(true, sl::Release);
        }

        size_t remaining = 0;
        for (size_t i = 0; i < dom.actors.Size(); i++)
        {
            auto& actor = dom.actors[i];

            if (actor.engine.done)
                continue;

            if (ActorPassedEpoch(actor, dom.engine.targetEpoch))
                actor.engine.done = true;
            else
                remaining++;
        }

        if (remaining != 0)
        {
            //if we're here someone is still in the target epoch, set the work
            //item to run at a later time and gtfo.

            if (dom.engine.expedite.Exchange(false, sl::AcqRel))
            {
                //someone is in a hurry (expedite flag was set), IPI any cpus
                //that we're waiting on.
                for (size_t i = 0; i < dom.actors.Size(); i++)
                {
                    if (!dom.actors[i].engine.done)
                        dom.nudge(dom, i);
                }

                QueueWorkItem(&dom.engine.workItem, {});
            }
            else
            {
                const auto now = GetMonotonicTime();
                const auto interval = sl::TimeCount(EbrWorkerIntervalMicros, 
                    sl::Micros);
                dom.engine.clockEvent.expiry = now.epoch 
                    + interval.Rebase(now.Frequency).ticks;

                AddClockEvent(&dom.engine.clockEvent);
            }

            return;
        }
        dom.engine.inFlight.Store(false, sl::Release);

        //it's safe to run callbacks up to (and including) target epoch.
        //NOTE: since we allow ebr items to be enqueued in any order we need to
        //filter through the whole list of each actor, some can be retired,
        //others need to go back on the list.

        for (size_t i = 0; i < dom.actors.Size(); i++)
        {
            auto& actor = dom.actors[i];
            EbrItemList retirees {};
            EbrItemList keep {};

            actor.listLock.Lock();
            while (!actor.list.Empty())
            {
                auto* item = actor.list.PopFront();

                if (EpochLt(item->epoch, dom.engine.targetEpoch))
                {
                    retirees.PushBack(item);
                    actor.listLength--;
                    dom.pending.Sub(1, sl::Relaxed);
                }
                else
                    keep.PushBack(item);
            }

            while (!keep.Empty())
                actor.list.PushBack(keep.PopFront());
            actor.listLock.Unlock();

            while (!retirees.Empty())
            {
                auto* item = retirees.PopFront();
                item->callback(item);
            }
        }

        if (dom.pending.Load(sl::Acquire) != 0)
            QueueWorkItem(&dom.engine.workItem, {});
    }

    NpkStatus ResetEbrDomain(EbrDomain& dom, size_t actorCount,
        EbrNudgeActor nudge)
    {
        if (nudge == nullptr)
            return NpkStatus::InvalidArg;
        if (actorCount == 0)
            return NpkStatus::InvalidArg;

        auto result = ResetWorkItem(&dom.engine.workItem, EbrWorker, &dom);
        if (result != NpkStatus::Success)
            return result;

        result = ResetDpc(&dom.engine.clockDpc, EbrDpcCallback, &dom, true);
        if (result != NpkStatus::Success)
            return result;

        const size_t len = actorCount * sizeof(EbrActor);
        void* ptr = PoolAllocWired(len, EbrHeapTag);
        if (ptr == nullptr)
            return NpkStatus::Shortage;

        auto* actors = static_cast<EbrActor*>(ptr);
        for (size_t i = 0; i < actorCount; i++)
            new (&actors[i]) EbrActor {};

        dom.engine.clockEvent.dpc = &dom.engine.clockDpc;
        dom.nudge = nudge;
        dom.actors = { actors, actorCount };

        return NpkStatus::Success;
    }

    void NudgeEpoch(EbrDomain& dom, size_t who)
    {
        NPK_ASSERT(who < dom.actors.Size());

        const auto observed = dom.epoch.Load(sl::Acquire);
        auto& actor = dom.actors[who];

        actor.epoch.Store(observed, sl::Release);
    }

    void EnterNoEpochState(EbrDomain& dom, size_t who)
    {
        NPK_ASSERT(who < dom.actors.Size());

        auto& actor = dom.actors[who];
        actor.idle.Add(1, sl::Release);
    }

    void ExitNoEpochState(EbrDomain& dom, size_t who)
    {
        NPK_ASSERT(who < dom.actors.Size());

        auto& actor = dom.actors[who];
        actor.idle.Add(1, sl::Acquire);
    }

    NpkStatus EbrCall(EbrDomain& dom, size_t who, EbrItem* item)
    {
        if (item == nullptr)
            return NpkStatus::InvalidArg;
        if (item->callback == nullptr)
            return NpkStatus::InvalidArg;
        if (who >= dom.actors.Size())
            return NpkStatus::InvalidArg;

        auto& actor = dom.actors[who];
        size_t pending = 0;

        item->epoch = dom.epoch.Load(sl::Acquire);

        actor.listLock.Lock();
        actor.list.PushBack(item);
        pending = ++actor.listLength;
        actor.listLock.Unlock();

        dom.pending.Add(1, sl::Relaxed);

        if (pending >= ExpediteThreshold)
            dom.engine.expedite.Store(true, sl::Release);

        QueueWorkItem(&dom.engine.workItem, {});

        return NpkStatus::Success;
    }

    static void EbrFenceCallback(EbrItem* item)
    {
        auto* fence = reinterpret_cast<EbrFence*>(item);

        SetCondition(fence->condition);
    }

    void EbrSync(EbrDomain& dom, size_t who)
    {
        Condition cond {};
        ResetCondition(&cond, 1);

        EbrFence fence {};
        fence.item.callback = EbrFenceCallback;
        fence.condition = &cond;

        EbrCall(dom, who, &fence.item);

        WaitEntry entry {};
        WaitOne(&cond, &entry, sl::NoTimeout);
    }

    void EbrBarrier(EbrDomain& dom)
    {
        Condition cond {};
        ResetCondition(&cond, dom.actors.Size());

        const size_t len = dom.actors.Size() * sizeof(EbrFence);
        auto* ptr = PoolAllocWired(len, EbrHeapTag);
        NPK_ASSERT(ptr != nullptr);

        auto* fences = static_cast<EbrFence*>(ptr);
        for (size_t i = 0; i < dom.actors.Size(); i++)
        {
            new (&fences[i]) EbrFence {};
            fences[i].condition = &cond;
            fences[i].item.callback = EbrFenceCallback;

            EbrCall(dom, i, &fences[i].item);
        }

        WaitEntry entry {};
        WaitOne(&cond, &entry, sl::NoTimeout);

        PoolFreeWired(ptr, len, EbrHeapTag);
    }

    RcuReadToken RcuReadLock()
    {
        const auto prev = RaiseIpl(Ipl::Dpc);
        sl::AtomicSignalFence(sl::AcqRel);

        return prev;
    }

    void RcuReadUnlock(RcuReadToken token)
    {
        sl::AtomicSignalFence(sl::AcqRel);
        LowerIpl(token);
    }

    NpkStatus RcuCall(RcuItem* item)
    {
        const auto who = MyCoreId() - MySystemDomain().smpBase;

        return EbrCall(MySystemDomain().rcu, who, item);
    }

    void RcuSync()
    {
        const auto who = MyCoreId() - MySystemDomain().smpBase;

        EbrSync(MySystemDomain().rcu, who);
    }

    void RcuBarrier()
    {
        EbrBarrier(MySystemDomain().rcu);
    }

    CPU_LOCAL(sl::Atomic<bool>, quiescentPending);

    void Private::ArmPendingRcuQuiesce()
    {
        quiescentPending->Store(true, sl::Relaxed);
    }

    void Private::CheckPendingRcuQuiesce()
    {
        if (!quiescentPending->Exchange(false, sl::Relaxed))
            return;

        auto& dom = MySystemDomain();
        if (dom.rcu.actors.Size() == 0)
            return;

        NudgeEpoch(dom.rcu, MyCoreId() - dom.smpBase);
        QueueWorkItem(&dom.rcu.engine.workItem, {});
    }
}
