#include <private/Core.hpp>
#include <lib/Maths.hpp>

//NOTE: lock ordering in the scheduler is that a thread's lock must always be
//taken before a scheduler's lock.
namespace Npk
{
    constexpr CpuId NoAffinity = static_cast<CpuId>(~0);
    constexpr uint8_t NicenessBias = 20;
    constexpr size_t MaxPriorityInheritenceDepth = 8;
    constexpr size_t PriorityScale = 4;
    constexpr size_t RtQueueCount = 
        ((MaxRtPriority - MinRtPriority) >> PriorityScale) + 1;
    constexpr size_t TsQueueCount = 
        ((MaxTsPriority - MinTsPriority) >> PriorityScale) + 1;

    //TODO: expose these constants as tunables via the command line
    constexpr size_t NicenessScale = 100;
    constexpr size_t InteractivityThreshold = 30;
    constexpr size_t AffinityHysteresis = 12;
    constexpr size_t MinQuantum = 5;
    constexpr size_t MaxQuantum = 100;
    constexpr size_t QuantumPriorityScale = 1;

    struct SchedGroup;

    struct SchedStatus
    {
        uint32_t totalLoad : 8;
        uint32_t stealableLoad : 8;
        uint32_t activePriority : 8;
        uint32_t activeIsInteractive : 1;
    };
    static_assert(sizeof(SchedStatus) == sizeof(uint32_t));

    struct LocalScheduler
    {
        CpuId cpuId;

        IplSpinLock<Ipl::Dpc> queuesLock;
        ThreadQueue rtQueues[RtQueueCount];
        ThreadQueue tsQueues[TsQueueCount];
        ThreadQueue idleQueue;
        size_t runnableCount;

        ClockEvent quantumEvent;
        Dpc quantumEventDpc;
        sl::TimePoint quantumStart;
        sl::ListHook groupHook;
        SchedGroup* group;

        ThreadContext* prevThread;
        ThreadContext* idle;
        sl::Atomic<ThreadContext*> nextThread;
        sl::Atomic<SchedStatus> status;
        sl::Atomic<bool> switchPending;
        sl::Atomic<bool> quantumEventArmed;
        uint8_t stealableLoad;
        uint8_t totalLoad;
    };

    using LocalSchedList = sl::List<LocalScheduler, &LocalScheduler::groupHook>;

    struct SchedGroup
    {
        size_t id;
        uint8_t migrationCost;
        uint8_t basePerfCapacity; //min(all members), 255 = fastest
        uint8_t baseEffiency; //min(all members), 255 = most efficient
        LocalSchedList members;

        sl::ListHook listHook;
    };

    using SchedGroupList = sl::List<SchedGroup, &SchedGroup::listHook>;

    struct CleanupJobs
    {
        IplSpinLock<Ipl::Dpc> lock;
        ThreadQueue threads;
    };

    static sl::SxSpinLock groupsLock;
    static SchedGroupList groups; //TODO: move into `SystemDomain`
    SchedGroup defaultGroup; //TODO: get rid of this

    CPU_LOCAL(LocalScheduler, localSched);
    CPU_LOCAL(CleanupJobs, cleanup);

    static uint8_t EffectivePriority(ThreadContext* thread)
    {
        auto& data = thread->scheduling;

        auto value = sl::Max(data.boostPriority, data.basePriority);
        if (thread->scheduling.agingBoost)
            value = sl::Max(value, MaxTsPriority);

        return value;
    }

    static LocalScheduler& RemoteSched(CpuId which)
    {
        if (which == MyCoreId())
            return *localSched;

        auto* status = RemoteStatus(which);
        NPK_ASSERT(status != nullptr);

        return *status->scheduler;
    }

    static void UpdateInteractivity(ThreadContext* thread)
    {
        auto& data = thread->scheduling;

        if (data.runTime == 0 && data.sleepTime == 0)
        {
            data.isInteractive = false;

            return;
        }

        const auto score = (static_cast<uint64_t>(data.runTime) * 100) 
            / (static_cast<uint64_t>(data.runTime) + data.sleepTime);
        const auto offset = static_cast<size_t>(data.niceness - NicenessBias) 
            * NicenessScale / 100;
        const auto threshold = sl::Clamp<size_t>(
            InteractivityThreshold + offset, 0, 100);

        data.isInteractive = score < threshold;

        //scale back runtime and sleeptime values if necessary to avoid
        //overflow
        constexpr uint32_t Threshold = 1u << 30;
        if (data.runTime > Threshold || data.sleepTime > Threshold)
        {
            data.runTime >>= 1;
            data.sleepTime >>= 1;
        }
    }

    //NOTE: assumes sched.queuesLock is held
    static void PushThread(LocalScheduler& sched, ThreadContext* thread)
    {
        auto SortFunc = [](ThreadContext* lhs, ThreadContext* rhs) -> bool
        {
            const auto l = EffectivePriority(lhs);
            const auto r = EffectivePriority(rhs);

            return l > r;
        };

        UpdateInteractivity(thread);

        const auto priority = EffectivePriority(thread);

        if (thread->scheduling.isInteractive)
            sched.rtQueues[0].InsertSorted(thread, SortFunc);
        else if (priority >= MinRtPriority)
        {
            const auto index = (priority - MinRtPriority) >> PriorityScale;
            sched.rtQueues[index].InsertSorted(thread, SortFunc);
        }
        else if (priority >= MinTsPriority)
        {
            const auto index = (priority - MinTsPriority) >> PriorityScale;
            sched.tsQueues[index].InsertSorted(thread, SortFunc);
            sched.runnableCount++;
        }
        else
            sched.idleQueue.PushBack(thread);

        thread->scheduling.inRunQueue = true;
    }

    //NOTE: assumes sched.queuesLock is held
    static ThreadContext* PopThread(LocalScheduler& sched)
    {
        for (size_t i = RtQueueCount; i != 0; i--)
        {
            const size_t index = i - 1;

            if (sched.rtQueues[index].Empty())
                continue;

            auto* thread = sched.rtQueues[index].PopFront();
            if (!thread->scheduling.isPinned)
                sched.stealableLoad--;
            thread->scheduling.inRunQueue = false;

            return thread;
        }

        for (size_t i = TsQueueCount; i != 0; i--)
        {
            const size_t index = i - 1;

            if (sched.tsQueues[index].Empty())
                continue;

            auto* thread = sched.tsQueues[index].PopFront();
            if (!thread->scheduling.isPinned)
                sched.stealableLoad--;
            sched.runnableCount--;
            thread->scheduling.inRunQueue = false;

            return thread;
        }

        auto* thread = sched.idleQueue.PopFront();
        if (thread != nullptr)
            thread->scheduling.inRunQueue = false;

        return thread;
    }

    //NOTE: assumes thread->sched.lock + sched.queuesLock is held
    static void RemoveThread(LocalScheduler& sched, ThreadContext* thread,
        uint8_t threadPriority)
    {
        thread->scheduling.inRunQueue = false;

        if (thread->scheduling.isInteractive)
        {
            sched.rtQueues[0].Remove(thread);

            return;
        }

        if (threadPriority >= MinRtPriority)
        {
            const auto index = (threadPriority - MinRtPriority) >>PriorityScale;
            sched.rtQueues[index].Remove(thread);

            return;
        }

        if (threadPriority >= MinTsPriority)
        {
            const auto index = (threadPriority - MinTsPriority) >>PriorityScale;
            sched.tsQueues[index].Remove(thread);
            sched.runnableCount--;

            return;
        }

        sched.idleQueue.Remove(thread);
    }

    //NOTE: cannot be holding scheduler or thread locks when calling
    static void SetNextThread(LocalScheduler& sched, ThreadContext* thread)
    {
        auto prev = sched.nextThread.Exchange(thread, sl::AcqRel);
        if (prev == nullptr)
            return;

        sl::ScopedLock prevLock(prev->scheduling.lock);
        sl::ScopedLock queueLock(sched.queuesLock);
        PushThread(sched, prev);

        if (!prev->scheduling.isPinned)
            sched.stealableLoad++;
    }

    static void QuantumExpired(Dpc* self, void* arg)
    {
        (void)self;

        auto& sched = *static_cast<LocalScheduler*>(arg);
        auto* thread = GetCurrentThread();

        const auto time = GetMonotonicTime().epoch - sched.quantumStart.epoch;
        thread->scheduling.runTime += time;
        
        sched.switchPending.Exchange(true, sl::Release);
    }

    static void ArmQuantumEvent(LocalScheduler& sched, ThreadContext* thread)
    {
        sched.quantumStart = GetMonotonicTime();

        const size_t runnable = sl::Max(1ul, sched.runnableCount);
        const size_t baseQuantum = sl::Clamp(MaxQuantum / runnable, MinQuantum,
            MaxQuantum);

        const auto priority = EffectivePriority(thread) - MinTsPriority;
        const auto priorityRange = MaxTsPriority - MinTsPriority;
        const auto priorityBonus = 
            (baseQuantum * priority * QuantumPriorityScale)
            / (priorityRange * 100);
        const size_t quantum = sl::Clamp(baseQuantum + priorityBonus,
            MinQuantum, MaxQuantum);

        ResetDpc(&sched.quantumEventDpc, QuantumExpired, &sched, true);
        sched.quantumEvent.expiry = sched.quantumStart.epoch 
            + (quantum * sched.quantumStart.Frequency / sl::Millis);
        sched.quantumEvent.dpc = &sched.quantumEventDpc;

        AddClockEvent(&sched.quantumEvent);
        sched.quantumEventArmed.Store(true, sl::Release);
    }

    //NOTE: expects thread->scheduling.lock to be held!
    static bool WouldPreemptOn(ThreadContext* thread, LocalScheduler& sched)
    {
        const auto status = sched.status.Load(sl::Acquire);
        const auto activePriority = status.activePriority;
        const auto newPriority = EffectivePriority(thread);

        if (activePriority == IdlePriority)
            return newPriority > IdlePriority;

        if (newPriority >= MinRtPriority)
        {
            if (activePriority < MinRtPriority)
                return true;

            return newPriority > activePriority;
        }

        if (activePriority >= MinRtPriority)
            return false;

        const bool activeInteractive = status.activeIsInteractive;
        const auto newInteractive = thread->scheduling.isInteractive;
        if (newInteractive && !activeInteractive)
            return true;
        if (!newInteractive && activeInteractive)
            return false;

        return newPriority > activePriority;
    }

    //NOTE: expects thread->scheduling.lock to be held!
    static CpuId SelectScheduler(ThreadContext* context)
    {
        NPK_ASSERT(context != nullptr);

        auto& data = context->scheduling;

        //1. hard affinity.
        if (data.isPinned)
            return data.affinity;

        //2. soft affinity is used if the target core has a moderate load.
        if (data.affinity != NoAffinity)
        {
            auto status = RemoteSched(data.affinity).status.Load(sl::Acquire);
            if (status.stealableLoad < AffinityHysteresis)
                return data.affinity;
        }

        //3. look at cpus in the local group.
        //TODO: if the thread has a soft affinity it should be the local group
        //of that cpu, since there may be shared lower level caches - speed!
        groupsLock.AcquireShared();
        auto group = localSched->group;
        for (auto it = group->members.Begin(); it != group->members.End(); ++it)
        {
            if (!WouldPreemptOn(context, *it))
                continue;
            groupsLock.ReleaseShared();

            return it->cpuId;
        }

        //4. all else failed, walk the topology tree of scheduling groups
        //to find a cpu where this thread would preempt on, and if that fails
        //find the least loaded cpu and enqueue the thread there.
        CpuId preemptCandidate = NoAffinity;
        uint32_t preemptLoad = ~0u;
        CpuId leastLoaded = NoAffinity;
        uint32_t leastLoad = ~0u;

        const auto hint = context->scheduling.powerHint;
        for (auto g = groups.Begin(); g != groups.End(); ++g)
        {
            for (auto c = g->members.Begin(); c != g->members.End(); ++c)
            {
                const auto status = c->status.Load(sl::Acquire);
                const auto perfScale = RemoteStatus(c->cpuId)->
                    performanceCapacity.Load(sl::AcqRel);

                //take into consideration the cost of migrating between
                //logical groups.
                uint32_t topologyCost = 0;
                if (&*g != group)
                    topologyCost = g->migrationCost;

                //also consider thread and cpu power hints
                uint32_t powerBias = 0;
                if (hint == PowerHint::Performance)
                    powerBias = g->basePerfCapacity;
                else if (hint == PowerHint::Efficient)
                    powerBias = g->baseEffiency;

                auto load = static_cast<uint32_t>(status.totalLoad) * 255;
                load = load / sl::Max<uint32_t>(1u, perfScale);
                auto adjustedLoad = load + topologyCost;
                if (adjustedLoad > powerBias)
                    adjustedLoad -= powerBias;
                else
                    adjustedLoad = 0;

                if (WouldPreemptOn(context, *c))
                {
                    if (adjustedLoad < preemptLoad)
                    {
                        preemptCandidate = c->cpuId;
                        preemptLoad = adjustedLoad;
                    }
                }
                else
                {
                    if (adjustedLoad < leastLoad)
                    {
                        leastLoaded = c->cpuId;
                        leastLoad = adjustedLoad;
                    }
                }
            }
        }
        groupsLock.ReleaseShared();

        if (preemptCandidate != NoAffinity)
            return preemptCandidate;
        if (leastLoaded != NoAffinity)
            return leastLoaded;

        //5. catch-all case, select the current cpu.
        return MyCoreId();
    }

    static void EndYield()
    {
        auto* current = GetCurrentThread();
        auto* prev = localSched->prevThread;
        NPK_ASSERT(prev != nullptr);

        localSched->prevThread = nullptr;

        SchedStatus status;
        status.activeIsInteractive = current->scheduling.isInteractive;
        status.activePriority = EffectivePriority(current);
        status.totalLoad = localSched->totalLoad;
        status.stealableLoad = localSched->stealableLoad;
        localSched->status.Store(status, sl::Release);

        prev->scheduling.agingBoost = false;
        switch (prev->scheduling.state)
        {
        case ThreadState::WaitPending:
            //hardware context is up to date, thread can safely be moved
            //to waiting state.
            prev->scheduling.state = ThreadState::Waiting;
            localSched->totalLoad--;
            break;

        case ThreadState::Executing:
        {
            if (prev == localSched->idle)
                break; //idle thread shouldn't go back in a queue

            prev->scheduling.state = ThreadState::Ready;
            auto& targetSched = RemoteSched(prev->scheduling.affinity);

            sl::ScopedLock scopeLock(targetSched.queuesLock);
            PushThread(targetSched, prev);

            if (!prev->scheduling.isPinned)
                targetSched.stealableLoad++;
            break;
        }

        case ThreadState::Waiting:
            localSched->totalLoad--;
            break;

        case ThreadState::Dead:
            localSched->totalLoad--;
            cleanup->lock.Lock();
            cleanup->threads.PushBack(prev);
            cleanup->lock.Unlock();
            break;

        default:
            break;
        };

        //unlock in reverse order of acquisition. Yield() raises IPL to Dpc
        //before acquiring these locks, so we can unlock in reverse order
        //and lower IPL.
        if ((uintptr_t)current < (uintptr_t)prev)
        {
            prev->scheduling.lock.Unlock();
            current->scheduling.lock.Unlock();
        }
        else
        {
            current->scheduling.lock.Unlock();
            prev->scheduling.lock.Unlock();
        }
        (void)prev;

        const auto priority = EffectivePriority(current);
        if (priority >= MinTsPriority && priority <= MaxTsPriority)
            ArmQuantumEvent(*localSched, current);

        LowerIpl(Ipl::Passive);

        auto& dom = MySystemDomain();
        NudgeEpoch(dom.rcu, MyCoreId() - dom.smpBase);
    }

    NpkStatus ResetThread(ThreadContext* thread)
    {
        if (thread == nullptr)
            return NpkStatus::InvalidArg;

        auto& data = thread->scheduling;
        if (data.state != ThreadState::Dead)
            return NpkStatus::InvalidArg;

        data.lock.Lock();
        if (!data.heldLocks.Empty() || !data.waitingOn.Empty())
        {
            data.lock.Unlock();
            Log("Thread %p exists in dead state but with locks held",
                LogLevel::Error, thread);

            return NpkStatus::InUse;
        }

        sl::MemSet(&thread->accounting, 0, sizeof(thread->accounting));
        data.context = nullptr;
        data.affinity = NoAffinity;
        data.sleepTime = 0;
        data.runTime = 0;
        data.basePriority = IdlePriority;
        data.boostPriority = 0;
        data.isPinned = false;
        data.isInteractive = false;
        data.niceness = NicenessBias;
        data.agingBoost = false;
        data.inRunQueue = false;
        data.lock.Unlock();

        return NpkStatus::Success;
    }

    static void EnterNewThread(void* arg, void(*Entry)(void*))
    {
        NPK_ASSERT(Entry != nullptr);

        EndYield();

        Entry(arg);
        NPK_UNREACHABLE();
    }

    NpkStatus PrepareThread(ThreadContext* thread, uintptr_t entry,
        uintptr_t arg, uintptr_t stack, sl::Opt<CpuId> affinity)
    {
        if (thread == nullptr)
            return NpkStatus::InvalidArg;

        sl::ScopedLock scopeLock(thread->scheduling.lock);
        if (thread->scheduling.state != ThreadState::Dead)
            return NpkStatus::InUse;

        if (affinity.HasValue())
        {
            thread->scheduling.affinity = *affinity;
            thread->scheduling.isPinned = true;
        }

        const auto stub = reinterpret_cast<uintptr_t>(EnterNewThread);
        HwPrimeThread(&thread->scheduling.context, stub, entry, 
            arg, stack);
        thread->scheduling.state = ThreadState::Standby;

        return NpkStatus::Success;
    }

    [[noreturn]]
    void ExitThread(size_t code)
    {
        AssertIpl(Ipl::Passive);

        auto thread = GetCurrentThread();
        NPK_ASSERT(thread->scheduling.heldLocks.Empty());

        Log("Thread %p exiting with code %zu", LogLevel::Verbose, thread,
            code);

        thread->scheduling.lock.Lock();
        thread->scheduling.state = ThreadState::Dead;
        thread->scheduling.lock.Unlock();

        Yield();
        NPK_UNREACHABLE();
    }

    static ThreadContext* TryStealThread(LocalScheduler& sched)
    {
        //there's three phases to stealing a thread: first find the most
        //loaded scheduler in the local group/domain. Then we scan that
        //scheduler's queues until we find a stealable thread.
        //Once we have a candidate thread we can lock it and *then* lock
        //the scheduler queues to remove the thread. The third pass is just to
        //satisfy the locking order.
        groupsLock.AcquireShared();

        LocalScheduler* targetSched = nullptr;
        uint8_t schedLoad = 0;
        //TODO: check local group first!
        for (auto g = groups.Begin(); g != groups.End(); ++g)
        {
            for (auto c = g->members.Begin(); c != g->members.End(); ++c)
            {
                if (&*c == &sched)
                    continue;

                const auto status = c->status.Load(sl::Acquire);
                if (status.stealableLoad <= schedLoad)
                    continue;

                schedLoad = status.stealableLoad;
                targetSched = &*c;
            }
        }
        groupsLock.ReleaseShared();

        if (targetSched == nullptr || schedLoad == 0)
            return nullptr; //nothing to steal

        //we've got the target core, find a candidate thread.
        ThreadContext* thread = nullptr;
        targetSched->queuesLock.Lock();
        for (size_t i = 0; i < TsQueueCount; i++)
        {
            auto& qs = targetSched->tsQueues;

            if (qs[i].Empty())
                continue;

            for (auto it = qs[i].Begin(); it != qs[i].End(); ++it)
            {
                if (it->scheduling.isPinned)
                    continue;

                thread = &*it;
                break;
            }

            if (thread != nullptr)
                break;
        }
        targetSched->queuesLock.Unlock();

        if (thread == nullptr)
            return nullptr;

        //we've got a thread to steal, lock it + the scheduler queues and
        //perform the steal.
        thread->scheduling.lock.Lock();
        targetSched->queuesLock.Lock();
        if (thread->scheduling.state != ThreadState::Ready
            || !thread->scheduling.inRunQueue
            || thread->scheduling.affinity != targetSched->cpuId
            || thread->scheduling.isPinned)
        {
            targetSched->queuesLock.Unlock();
            thread->scheduling.lock.Unlock();

            return nullptr;
        }

        const auto priority = EffectivePriority(thread);
        RemoveThread(*targetSched, thread, priority);
        targetSched->stealableLoad--;
        targetSched->totalLoad--;

        targetSched->queuesLock.Unlock();

        thread->scheduling.affinity = sched.cpuId;
        thread->scheduling.lock.Unlock();

        sched.queuesLock.Lock();
        sched.totalLoad++;
        sched.queuesLock.Unlock();

        return thread;
    }

    void Yield()
    {
        AssertIpl(Ipl::Passive);
        RaiseIpl(Ipl::Dpc);

        auto& sched = *localSched;
        auto* current = GetCurrentThread();

        //cancel the clock event for the current quantum if it's active.
        //If we're too late and the event has already fired (unlikely but
        //not impossible) we'll need to spin on the DPC. The spinning case can
        //only happen if the clock event for this cpu was processed by another
        //cpu. There are ways this can happen, but its rare.
        if (sched.quantumEventArmed.Load(sl::Relaxed))
        {
            if (RemoveClockEvent(&sched.quantumEvent))
            {
                auto runtime = GetMonotonicTime().epoch 
                    - sched.quantumStart.epoch;
                current->scheduling.runTime += runtime;
            }
            else
                SpinUntilDpcCompleted(&sched.quantumEventDpc);
            sched.quantumEventArmed.Store(false, sl::Release);
        }

        auto next = sched.nextThread.Exchange(nullptr, sl::Acquire);
        if (next == nullptr)
        {
            sl::ScopedLock qlock(sched.queuesLock);
            next = PopThread(sched);
        }
        if (next == nullptr)
            next = TryStealThread(sched);
        if (next == nullptr)
            next = sched.idle;

        if (next == current)
        {
            NPK_ASSERT(current->scheduling.state == ThreadState::Executing);
            ArmQuantumEvent(sched, current);
            LowerIpl(Ipl::Passive);

            return;
        }

        //thread locks are considered equal rank, acquire them based on
        //address.
        if ((uintptr_t)current < (uintptr_t)next)
        {
            current->scheduling.lock.Lock();
            next->scheduling.lock.Lock();
        }
        else
        {
            next->scheduling.lock.Lock();
            current->scheduling.lock.Lock();
        }

        SetCycleAccount(CycleAccount::Kernel);
        NPK_ASSERT(sched.prevThread == nullptr);
        sched.prevThread = current;

        next->scheduling.state = ThreadState::Executing;
        SetCurrentThread(next);

        //actual content switch happens here, EndYield() implements to exit
        //path of a context switch and takes care of unlock the thread structs.
        HwSwitchThread(&current->scheduling.context, next->scheduling.context);
        EndYield();
    }

    void EnqueueThread(ThreadContext* thread)
    {
        auto& data = thread->scheduling;

        sl::ScopedLock scopeLock(data.lock);
        NPK_ASSERT(data.state == ThreadState::Standby);

        const auto affinity = SelectScheduler(thread);
        data.affinity = affinity;
        data.state = ThreadState::Ready;

        auto& sched = RemoteSched(affinity);
        if (WouldPreemptOn(thread, sched))
        {
            sched.queuesLock.Lock();
            sched.totalLoad++;
            sched.queuesLock.Unlock();

            scopeLock.Release();
            SetNextThread(sched, thread);

            sched.switchPending.Store(true, sl::Release);
            if (affinity != MyCoreId())
                NudgeCpu(affinity);
        }
        else
        {
            sl::ScopedLock qlock(sched.queuesLock);
            PushThread(sched, thread);

            sched.totalLoad++;
            if (!data.isPinned)
                sched.stealableLoad++;
        }
    }

    void SetThreadNiceness(ThreadContext* thread, uint8_t value)
    {
        if (thread == nullptr)
            return;

        value = sl::Clamp(value, MinNiceness, MaxNiceness);

        sl::ScopedLock scopeLock(thread->scheduling.lock);
        thread->scheduling.niceness = value;
    }

    void SetThreadPriority(ThreadContext* thread, uint8_t value)
    {
        if (thread == nullptr)
            return;

        sl::ScopedLock scopeLock(thread->scheduling.lock);

        auto oldEffective = EffectivePriority(thread);
        thread->scheduling.basePriority = value;
        auto newEffective = EffectivePriority(thread);

        if (oldEffective == newEffective)
            return;

        switch (thread->scheduling.state)
        {
        case ThreadState::Ready:
        {
            auto& oldSched = RemoteSched(thread->scheduling.affinity);
            const auto oldAffinity = thread->scheduling.affinity;

            oldSched.queuesLock.Lock();
            RemoveThread(oldSched, thread, oldEffective);
            oldSched.queuesLock.Unlock();

            CpuId targetCpu;
            if (oldEffective > newEffective)
                targetCpu = oldAffinity;
            else
                targetCpu = SelectScheduler(thread);

            if (targetCpu == oldAffinity)
            {
                oldSched.queuesLock.Lock();
                PushThread(oldSched, thread);
                oldSched.queuesLock.Unlock();
            }
            else
            {
                //thread needs to migrate!

                oldSched.queuesLock.Lock();
                oldSched.totalLoad--;
                if (!thread->scheduling.isPinned)
                    oldSched.stealableLoad--;
                oldSched.queuesLock.Unlock();

                thread->scheduling.affinity = targetCpu;

                auto& targetSched = RemoteSched(targetCpu);

                targetSched.queuesLock.Lock();
                PushThread(targetSched, thread);
                targetSched.totalLoad++;
                if (!thread->scheduling.isPinned)
                    targetSched.stealableLoad++;
                targetSched.queuesLock.Unlock();

                if (WouldPreemptOn(thread, targetSched))
                {
                    targetSched.switchPending.Store(true, sl::Release);
                    if (targetCpu != MyCoreId())
                        NudgeCpu(targetCpu);
                }
            }
            break;
        }

        case ThreadState::Executing:
        {
            if (newEffective >= oldEffective)
                break;

            auto& sched = RemoteSched(thread->scheduling.affinity);
            auto status = sched.status.Load(sl::Relaxed);

            status.activePriority = newEffective;
            sched.status.Store(status, sl::Release);
            sched.switchPending.Store(true, sl::Release);

            if (thread->scheduling.affinity != MyCoreId())
                NudgeCpu(thread->scheduling.affinity);
            break;
        }

        default:
            break;
        }
    }

    void SetThreadAffinity(ThreadContext* thread, CpuId who)
    {
        if (thread == nullptr || who == NoAffinity)
            return;

        auto& data = thread->scheduling;
        sl::ScopedLock threadLock(data.lock);

        if (data.affinity == who && data.isPinned)
            return;

        const auto oldAffinity = data.affinity;
        const auto wasPinned = data.isPinned;

        data.affinity = who;
        data.isPinned = true;

        switch (data.state)
        {
        case ThreadState::Ready:
        {
            const auto oldEffective = EffectivePriority(thread);
            auto& oldSched = RemoteSched(oldAffinity);
            auto& newSched = RemoteSched(who);

            oldSched.queuesLock.Lock();
            RemoveThread(oldSched, thread, oldEffective);
            oldSched.totalLoad--;
            if (!wasPinned)
                oldSched.stealableLoad--;
            oldSched.queuesLock.Unlock();

            newSched.queuesLock.Lock();
            PushThread(newSched, thread);
            newSched.totalLoad++;
            newSched.queuesLock.Unlock();

            if (WouldPreemptOn(thread, newSched))
            {
                newSched.switchPending.Store(true, sl::Release);
                if (who != MyCoreId())
                    NudgeCpu(who);
            }
            break;
        }

        case ThreadState::Executing:
        {
            auto& sched = RemoteSched(oldAffinity);

            sched.switchPending.Store(true, sl::Release);
            if (oldAffinity != MyCoreId())
                NudgeCpu(oldAffinity);
            break;
        }

        default:
            break;
        }
    }

    void SetThreadPowerHint(ThreadContext* thread, PowerHint hint)
    {
        if (thread == nullptr)
            return;

        sl::ScopedLock scopeLock(thread->scheduling.lock);
        thread->scheduling.powerHint = hint;

        //the new power hint will be taken into consideration the next time
        //the thread is looked at for selection. Currently I dont think there's
        //any benefit to re-evaluing it's runnable status for each cpu because
        //of a hint change.
    }

    void ClearThreadAffinity(ThreadContext* thread)
    {
        if (thread == nullptr)
            return;

        auto& data = thread->scheduling;
        sl::ScopedLock threadLock(data.lock);

        if (!data.isPinned)
            return;
        data.isPinned = false;

        if (data.state != ThreadState::Ready)
            return;

        auto& sched = RemoteSched(data.affinity);

        sched.queuesLock.Lock();
        sched.stealableLoad++;
        sched.queuesLock.Unlock();
    }

    sl::Opt<uint8_t> GetThreadNiceness(ThreadContext* thread)
    {
        if (thread == nullptr)
            return {};

        sl::ScopedLock lock(thread->scheduling.lock);

        return thread->scheduling.niceness;
    }

    sl::Opt<uint8_t> GetThreadPriority(ThreadContext* thread)
    {
        if (thread == nullptr)
            return {};

        sl::ScopedLock lock(thread->scheduling.lock);

        return thread->scheduling.basePriority;
    }

    sl::Opt<uint8_t> GetThreadEffectivePriority(ThreadContext* thread)
    {
        if (thread == nullptr)
            return {};

        sl::ScopedLock lock(thread->scheduling.lock);

        return EffectivePriority(thread);
    }

    sl::Opt<CpuId> GetThreadAffinity(ThreadContext* thread, bool& pinned)
    {
        if (thread == nullptr)
            return {};

        sl::ScopedLock lock(thread->scheduling.lock);
        pinned = thread->scheduling.isPinned;

        return thread->scheduling.affinity;
    }

    sl::Opt<PowerHint> GetThreadPowerHint(ThreadContext* thread)
    {
        if (thread == nullptr)
            return {};

        sl::ScopedLock lock(thread->scheduling.lock);

        return thread->scheduling.powerHint;
    }

    void Private::InitLocalScheduler(ThreadContext* idle)
    {
        auto& sched = *localSched;

        sched.cpuId = MyCoreId();
        sched.idle = idle;
        sched.group = &defaultGroup;
        sched.prevThread = nullptr;
        sched.nextThread.Store(nullptr, sl::Relaxed);
        sched.switchPending.Store(false, sl::Relaxed);
        sched.quantumEventArmed.Store(false, sl::Relaxed);
        sched.totalLoad = 0;
        sched.stealableLoad = 0;
        sched.runnableCount = 0;

        groupsLock.AcquireExclusive();
        defaultGroup.members.PushBack(&sched);
        if (groups.Empty())
            groups.PushBack(&defaultGroup);
        groupsLock.ReleaseExclusive();

        SchedStatus initStatus {};
        initStatus.activePriority = IdlePriority;
        sched.status.Store(initStatus, sl::Release);

        RemoteStatus(MyCoreId())->scheduler = &*localSched;

        idle->scheduling.affinity = MyCoreId();
        idle->scheduling.state = ThreadState::Executing;
        idle->scheduling.basePriority = IdlePriority;

        SetCurrentThread(idle);
    }

    void Private::OnPassiveRunLevel()
    {
        auto& sched = *localSched;

        if (sched.switchPending.Exchange(false, sl::Acquire))
            Yield();
    }

    static void ApplyPriorityBoost(ThreadContext* blocked, ThreadContext* owner,
        size_t depth)
    {
        NPK_ASSERT(blocked != nullptr);

        if (depth >= MaxPriorityInheritenceDepth)
            return;
        if (owner == nullptr)
            return;

        auto& ownerData = owner->scheduling;
        sl::ScopedLock ownerLock(ownerData.lock);
        //avoid a TOCTOU issue here and check the owner's state while holding
        //it's scheduling data lock.
        if (ownerData.state == ThreadState::Dead)
            return;

        const auto blockedPriority = EffectivePriority(blocked);
        if (blockedPriority <= EffectivePriority(owner))
            return;

        const auto oldPriority = EffectivePriority(owner);
        ownerData.boostPriority = sl::Max(ownerData.boostPriority, 
            blockedPriority);
        const auto newPriority = EffectivePriority(owner);

        if (oldPriority != newPriority)
        {
            auto& sched = RemoteSched(ownerData.affinity);

            if (ownerData.state == ThreadState::Ready)
            {
                sched.queuesLock.Lock();

                RemoveThread(sched, owner, oldPriority);
                PushThread(sched, owner);

                sched.queuesLock.Unlock();
            }
            else if (ownerData.state == ThreadState::Executing
                  || ownerData.state == ThreadState::WaitPending)
            {
                //executing thread's priority has changed, we need to update
                //the status for that scheduler to reflect the new priority.
                //NOTE: this is the exception to the local scheduler owning
                //it's atomic status field: in this case its safe because
                //Yield() (the other writer of this field) will acquire the
                //thread's scheduling data lock before updating the field,
                //a lock which we already hold.
                auto status = sched.status.Load(sl::Relaxed);
                status.activePriority = newPriority;
                sched.status.Store(status, sl::Release);
            }
        }

        const auto nextWaitables = ownerData.waitingOn;
        const bool ownerIsWaiting = ownerData.state == ThreadState::Waiting
            || ownerData.state == ThreadState::WaitPending;
        ownerLock.Release();

        if (!ownerIsWaiting || nextWaitables.Empty())
            return;

        for (size_t i = 0; i < nextWaitables.Size(); i++)
        {
            auto& what = nextWaitables[i];

            //only some types of waitables support priority inheritence,
            //filter out the ones which dont.
            if (what.waitable->type != WaitableType::Mutex
                && what.waitable->type != WaitableType::SxMutex)
                continue;

            auto* nextOwner = what.waitable->owner;
            ApplyPriorityBoost(owner, nextOwner, depth + 1);
        }
    }

    void Private::BeginWait(sl::Span<WaitEntry> waitingOn)
    {
        AssertIpl(Ipl::Dpc);

        auto thread = GetCurrentThread();

        if (thread == localSched->idle)
            NPK_ASSERT(!"Idle thread cannot block");

        thread->scheduling.lock.Lock();
        thread->scheduling.state = ThreadState::WaitPending;
        thread->scheduling.waitingOn = waitingOn;
        thread->scheduling.sleepBegin = GetMonotonicTime();
        thread->scheduling.lock.Unlock();

        //propagate any boosts from the current thread any PI-affected
        //waitables.
        for (size_t i = 0; i < waitingOn.Size(); i++)
        {
            auto waitable = waitingOn[i].waitable;
            if (waitable->type != WaitableType::Mutex
                && waitable->type != WaitableType::SxMutex)
                continue;

            ApplyPriorityBoost(thread, waitable->owner, 0);
        }

        localSched->switchPending.Store(true, sl::Release);
    }

    void Private::EndWait()
    {
        AssertIpl(Ipl::Dpc);
        
        auto* thread = GetCurrentThread();
        auto sleepEnd = GetMonotonicTime();
        auto sleepTime = sleepEnd.epoch - thread->scheduling.sleepBegin.epoch;

        sl::ScopedLock scopeLock(thread->scheduling.lock);
        thread->scheduling.sleepTime += sleepTime;

        thread->scheduling.waitingOn = {};
        //NOTE: no need to update the state here as thread is already executing
    }

    void Private::WakeThread(ThreadContext* thread)
    {
        NPK_ASSERT(thread != nullptr);

        thread->scheduling.lock.Lock();

        const auto state = thread->scheduling.state;
        if (state != ThreadState::Waiting && state != ThreadState::WaitPending)
        {
            //someone else woke the thread first.
            thread->scheduling.lock.Unlock();

            return;
        }

        thread->scheduling.waitingOn = {};

        if (state == ThreadState::WaitPending)
        {
            //thread is still executing - its register context has not been
            //saved yet. Revert to Executing so EndYield() re-queues it once
            //the context is saved, rather than loading a stale context and
            //making a real mess of the thread's stack.
            thread->scheduling.state = ThreadState::Executing;
            thread->scheduling.lock.Unlock();

            return;
        }

        thread->scheduling.state = ThreadState::Standby;
        thread->scheduling.lock.Unlock();

        EnqueueThread(thread);
    }
}
