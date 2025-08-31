#include <CorePrivate.hpp>

namespace Npk
{
    constexpr CpuId NoAffinity = static_cast<CpuId>(~0);
    constexpr uint8_t NicenessBias = 20;
    constexpr size_t PriorityScale = 4;
    constexpr size_t RtQueueCount = 
        ((MaxRtPriority - MinRtPriority) >> PriorityScale) + 1;
    constexpr size_t TsQueueCount = 
        ((MaxTsPriority - MinTsPriority) >> PriorityScale) + 1;

    union SchedStatus
    {
        uint32_t squish;
        struct
        {
            uint32_t load : 8;
            uint32_t activePriority : 8;
            uint32_t isInteractive : 1;
        };
    };
    
    //TODO: topology
    //TODO: calculate load
    //TODO: work balancing
    //TODO: determine interactivity status

    struct LocalScheduler
    {
        IplSpinLock<Ipl::Dpc> queuesLock;
        ThreadQueue rtQueues[RtQueueCount];
        ThreadQueue tsQueues[TsQueueCount];
        ThreadQueue idleQueue;

        ThreadContext* idleThread;
        ThreadContext* prevThread;

        //cleared locally, but may be set by other CPUs (see `nextThread`)
        sl::Atomic<bool> switchPending;
        //this is used by other CPUs in the same cohort to determine if they
        //should preempt this cpu's running thread.
        sl::Atomic<SchedStatus> status;
        //other CPUs will place a thread here if they want us to run it.
        sl::Atomic<ThreadContext*> nextThread;
    };

    struct CleanupJobs
    {
        IntrSpinLock lock;
        ThreadQueue queue;
    };

    CPU_LOCAL(LocalScheduler, static localSched);
    CPU_LOCAL(CleanupJobs, static cleanup);
    
    static LocalScheduler* RemoteSched(CpuId who)
    {
        auto remoteStatus = RemoteStatus(who);
        if (remoteStatus == nullptr)
            return nullptr;

        return remoteStatus->scheduler;
    }

    //NOTE: assumes this queue is locked (via its parent scheduler.lock)
    static ThreadContext* PopStealableThread(ThreadQueue& queue)
    {
        for (auto it = queue.Begin(); it != queue.End(); ++it)
        {
            it->scheduling.lock.Lock();
            const bool pinned = it->scheduling.isPinned;
            it->scheduling.lock.Unlock();

            if (pinned)
                continue;

            return &*it;
        }

        return nullptr;
    }

    static ThreadContext* PopThread(LocalScheduler& sched, bool stealing)
    {
        sl::ScopedLock scopeLock(sched.queuesLock);

        for (size_t i = RtQueueCount; i != 0; i--)
        {
            auto& queue = sched.rtQueues[i - 1];

            if (queue.Empty())
                continue;
            if (stealing)
                return PopStealableThread(queue);
            return queue.PopFront();
        }

        for (size_t i = TsQueueCount; i != 0; i--)
        {
            auto& queue = sched.tsQueues[i - 1];

            if (queue.Empty())
                continue;
            if (stealing)
                return PopStealableThread(queue);
            return queue.PopFront();
        }

        if (!sched.idleQueue.Empty())
        {
            if (stealing)
                return PopStealableThread(sched.idleQueue);
            return sched.idleQueue.PopFront();
        }

        return nullptr;
    }

    //NOTE: assumes thread->scheduling.lock is held!
    static ThreadQueue& GetQueue(LocalScheduler& sched, ThreadContext* thread)
    {
        auto& data = thread->scheduling;

        if (data.isInteractive)
            return sched.rtQueues[0];
        else if (data.dynPriority >= MinRtPriority)
        {
            const uint8_t index = (data.dynPriority - MinRtPriority) 
                >> PriorityScale;
            NPK_ASSERT(index < RtQueueCount);
            return sched.rtQueues[index];
        }
        else if (data.dynPriority >= MinTsPriority)
        {
            const uint8_t index = (data.dynPriority - MinTsPriority)
                >> PriorityScale;
            NPK_ASSERT(index < TsQueueCount);
            return sched.tsQueues[index];
        }
        else
            return sched.idleQueue;

    }
    
    //NOTE: assumes thread->scheduling.lock is held!
    static void PushThread(LocalScheduler& sched, ThreadContext* thread)
    {
        auto& queue = GetQueue(sched, thread);

        sl::ScopedLock scopeLock(sched.queuesLock);
        queue.PushBack(thread);
    }

    //NOTE: assumes thread->scheduling.lock is held!
    static void RemoveThread(LocalScheduler& sched, ThreadContext* thread)
    {
        auto& queue = GetQueue(sched, thread);

        sl::ScopedLock scopeLock(sched.queuesLock);
        queue.Remove(thread);
    }

    static void SetNextThread(LocalScheduler& sched, ThreadContext* thread)
    {
        auto prev = sched.nextThread.Exchange(thread, sl::AcqRel);

        if (prev != nullptr)
            PushThread(sched, prev);
    }

    //NOTE: assumes thread->scheduling.lock is held
    static size_t GenerateScore(ThreadContext* thread)
    {
        constexpr size_t ScalingFactor = 30;

        auto& data = thread->scheduling;

        if (data.sleepTime == 0)
            return 100;
        if (data.runTime == 0)
            return 0;

        size_t accum = 0;
        if (data.sleepTime > data.runTime)
            accum = ScalingFactor / (data.sleepTime / data.runTime);
        else
        {
            accum = (ScalingFactor / (data.runTime / data.sleepTime));
            accum += ScalingFactor;
        }

        accum += thread->scheduling.niceness;
        return accum;
    }

    //NOTE: assumes thread->scheduling.lock is held
    static void ReassesThread(ThreadContext* context)
    {} //TODO: 

    //NOTE: assumes thread->scheduling.lock is held
    static bool WouldPreemptOn(ThreadContext* thread, LocalScheduler* sched)
    {
        const SchedStatus status = sched->status.Load(sl::Acquire);
        auto& data = thread->scheduling;

        if (status.activePriority == IdlePriority)
            return true;
        if (!status.isInteractive && data.isInteractive
            && status.activePriority <= MaxTsPriority)
            return true;
        if (data.dynPriority > status.activePriority 
            && data.dynPriority >= MinRtPriority)
            return true;

        return false;
    }

    //NOTE: assumes thread->scheduling.lock is held
    static CpuId SelectScheduler(ThreadContext* thread)
    {
        auto& data = thread->scheduling;

        //TODO: *Actual* selection here, be topology aware.
        //Wishlist of things to support:
        //- hyperthreading (prefer to enqueue on physical, not logical duplicates)
        //- clusters (group of X, default of 4, cpus sharing threads).
        //- supercluster, basically a physical cpu package
        if (data.affinity != NoAffinity)
            return data.affinity;
        return MyCoreId();

    }

    static void EndYield()
    {
        auto current = GetCurrentThread();
        auto prevThread = localSched->prevThread;
        localSched->prevThread = nullptr;

        //update scheduler's state field to represent the now-current thread
        SchedStatus nextStatus = localSched->status.Load(sl::Acquire);
        nextStatus.isInteractive = current->scheduling.isInteractive;
        nextStatus.activePriority = current->scheduling.dynPriority;
        localSched->status.Store(nextStatus, sl::Release);

        GetCurrentThread()->scheduling.lock.Unlock();

        //fixup prevThread's state
        if (prevThread->scheduling.state == ThreadState::Executing)
        {
            prevThread->scheduling.state = ThreadState::Ready;

            if (prevThread->scheduling.affinity == MyCoreId())
                PushThread(*localSched, prevThread);
            else if (prevThread->scheduling.affinity != NoAffinity)
            {
                //someone else changed the thread's affinity while we were 
                //running it, put it into their queue.
                auto remoteSched = RemoteSched(prevThread->scheduling.affinity);
                NPK_ASSERT(remoteSched != nullptr);
                PushThread(*remoteSched, prevThread);
            }
        }
        else if (prevThread->scheduling.state == ThreadState::Dead)
        {
            sl::ScopedLock scopeLock(cleanup->lock);
            cleanup->queue.PushBack(prevThread);
        }
        //else: thread state was intentionally changed, leave it.

        prevThread->scheduling.lock.Unlock();
    }

    static void EnterNewThread(void* arg, void(*Entry)(void*))
    {
        NPK_ASSERT(Entry != nullptr);

        EndYield();

        Entry(arg);
        NPK_UNREACHABLE();
    }

    bool ResetThread(ThreadContext* thread)
    {
        NPK_CHECK(thread != nullptr, false);

        sl::ScopedLock scopeLock(thread->scheduling.lock);
        NPK_CHECK(thread->scheduling.state == ThreadState::Dead, false);
        
        thread->accounting.kernelNs = {};
        thread->accounting.userNs = {};

        thread->scheduling.context = nullptr;
        thread->scheduling.affinity = NoAffinity;
        thread->scheduling.sleepTime = 0;
        thread->scheduling.runTime = 0;
        thread->scheduling.basePriority = IdlePriority;
        thread->scheduling.dynPriority = thread->scheduling.basePriority;
        thread->scheduling.score = 0;
        thread->scheduling.isPinned = false;
        thread->scheduling.isInteractive = false;
        thread->scheduling.niceness = NicenessBias;

        return true;
    }

    bool PrepareThread(ThreadContext* thread, uintptr_t entry, uintptr_t arg, 
        uintptr_t stack, sl::Opt<CpuId> affinity)
    {
        NPK_CHECK(thread != nullptr, false);

        sl::ScopedLock scopeLock(thread->scheduling.lock);
        NPK_CHECK(thread->scheduling.state == ThreadState::Dead, false);

        if (affinity.HasValue())
        {
            thread->scheduling.affinity = *affinity;
            thread->scheduling.isPinned = true;
        }

        auto stub = reinterpret_cast<uintptr_t>(EnterNewThread);
        ArchPrimeThread(&thread->scheduling.context, stub, entry, arg, stack);
        thread->scheduling.state = ThreadState::Standby;

        return true;
    }

    void ExitThread(size_t code, void* data)
    { 
        auto thread = GetCurrentThread();

        thread->scheduling.lock.Lock();
        thread->scheduling.state = ThreadState::Dead;
        thread->scheduling.lock.Unlock();

        Log("Thread %p is exiting: code=0x%zx, data=%p", LogLevel::Info,
            thread, code, data);

        Yield();
        NPK_UNREACHABLE(); 
    }

    void Yield()
    {
        auto next = localSched->nextThread.Exchange(nullptr, sl::Acquire);
        if (next == nullptr)
            next = PopThread(*localSched, false);
        if (next == nullptr)
            next = localSched->idleThread;

        auto current = GetCurrentThread();
        NPK_ASSERT(current != next);

        //we're acquiring locks of the same rank, acquire by the lowest address
        //first to avoid deadlock.
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

        SetCycleAccount(CycleAccount::Kernel); //update thread's cycle counters
        localSched->prevThread = current;

        next->scheduling.state = ThreadState::Executing;
        SetCurrentThread(next);
        ArchSwitchThread(&current->scheduling.context, next->scheduling.context);
        EndYield();
    }

    void EnqueueThread(ThreadContext* thread)
    {
        NPK_CHECK(thread != nullptr, );

        auto& data = thread->scheduling;
        sl::ScopedLock threadLock(data.lock);
        NPK_CHECK(data.state == ThreadState::Standby, );

        data.affinity = SelectScheduler(thread);
        data.dynPriority = data.basePriority;
        data.score = GenerateScore(thread);
        data.state = ThreadState::Ready;

        auto targetSched = RemoteSched(data.affinity);
        NPK_ASSERT(targetSched != nullptr);

        if (WouldPreemptOn(thread, targetSched))
        {
            SetNextThread(*targetSched, thread);

            if (targetSched == &*localSched)
                localSched->switchPending.Store(true, sl::Release);
            else
                NudgeCpu(data.affinity);
        }
        else
            PushThread(*targetSched, thread);

        threadLock.Release();
        if (CurrentIpl() == Ipl::Passive)
            Private::OnPassiveRunLevel();
    }

    void SetThreadNiceness(ThreadContext* thread, uint8_t value)
    {} //TODO:

    void SetThreadPriority(ThreadContext* thread, uint8_t value)
    {
        NPK_CHECK(thread != nullptr, );
        NPK_CHECK(value != IdlePriority, );

        Log("Setting thread %p priority to %u", LogLevel::Verbose,
            thread, value);

        auto& data = thread->scheduling;
        sl::ScopedLock threadLock(data.lock);

        const auto prevBasePrio = data.basePriority;
        data.basePriority = value;
        data.dynPriority = value; //TODO: smarter handling

        if (data.state == ThreadState::Executing && prevBasePrio > value)
        {} //TODO: check if thread should be preempted by another
        else if (data.state == ThreadState::Standby && value > prevBasePrio
            && data.affinity != NoAffinity)
        {
            //TODO: check nearby cpus (req: topology) for possible preemption
            //there, in order to prioritize responsiveness

            auto sched = RemoteSched(data.affinity);
            NPK_ASSERT(sched != nullptr);
            if (WouldPreemptOn(thread, sched))
            {
                SetNextThread(*sched, thread);
                if (sched == &*localSched)
                    localSched->switchPending.Store(true, sl::Release);
                else
                    NudgeCpu(data.affinity);
            }
        }
        //TODO: state == ThreadState::Ready (i.e. on a runqueue)

        threadLock.Release();
        if (CurrentIpl() == Ipl::Passive)
            Private::OnPassiveRunLevel();
    }

    void SetThreadAffinity(ThreadContext* thread, CpuId who)
    {
        NPK_CHECK(thread != nullptr, );
        NPK_CHECK(who < MySystemDomain().smpControls.Size(), );

        Log("Setting thread %p affinity to %zu", LogLevel::Verbose, 
            thread, who);

        auto& data = thread->scheduling;
        sl::ScopedLock threadLock(data.lock);
        data.isPinned = true;

        const auto prevAffinity = data.affinity;
        data.affinity = who;
        if (prevAffinity == NoAffinity)
            return; //thread never ran, we're done here
        if (data.state == ThreadState::Standby)
            return; //thread is not running or enqueued
        if (prevAffinity == who)
            return; //thread is pinned to same core

        /* if we're here it means the thread needs to migrate cores,
         * there's two possible codepaths:
         * - the thread is executing, in which case we can set the pending
         *   switch flag on that cpu and nudge it. It will notice someone
         *   else changed the thread's affinity and add it to the appropriate
         *   runqueue.
         * - the thread is only queued for execution. This is pretty simple,
         *   remove it from its current queue and place it in the appropriate
         *   one, with the correct locks held of course.
         */
        auto remoteSched = RemoteSched(prevAffinity);
        NPK_ASSERT(remoteSched != nullptr);
        Log("Thread %p affinity change requires migration: %zu -> %zu.", 
            LogLevel::Trace, thread, prevAffinity, who);

        if (data.state == ThreadState::Executing)
        {
            remoteSched->switchPending.Store(true, sl::Release);
            NudgeCpu(prevAffinity);
        }
        else if (data.state == ThreadState::Ready)
        {
            RemoveThread(*remoteSched, thread);

            auto targetSched = RemoteSched(who);
            NPK_ASSERT(targetSched != nullptr);

            if (WouldPreemptOn(thread, targetSched))
                SetNextThread(*targetSched, thread);
            else
                PushThread(*targetSched, thread);
        }
    }

    void ClearThreadAffinity(ThreadContext* thread)
    {
        NPK_CHECK(thread != nullptr, );

        auto& data = thread->scheduling;
        
        sl::ScopedLock threadLock(data.lock);
        data.isPinned = false;
    }

    sl::Opt<uint8_t> GetThreadNiceness(ThreadContext* thread)
    {
        NPK_CHECK(thread != nullptr, {});

        sl::ScopedLock threadLock(thread->scheduling.lock);
        return thread->scheduling.niceness;
    }

    sl::Opt<uint8_t> GetThreadPriority(ThreadContext* thread)
    {
        NPK_CHECK(thread != nullptr, {});

        sl::ScopedLock threadLock(thread->scheduling.lock);
        return thread->scheduling.basePriority;
    }

    sl::Opt<CpuId> GetThreadAffinity(ThreadContext* thread, bool& pinned)
    {
        NPK_CHECK(thread != nullptr, {});

        sl::ScopedLock threadLock(thread->scheduling.lock);
        pinned = thread->scheduling.isPinned;

        auto affinity = thread->scheduling.affinity;
        if (affinity == NoAffinity)
            return {};
        return affinity;
    }

    void Private::InitLocalScheduler(ThreadContext* idle)
    {
        NPK_ASSERT(idle != nullptr);

        idle->scheduling.lock.Lock();
        idle->scheduling.state = ThreadState::Standby;
        idle->scheduling.basePriority = IdlePriority;
        idle->scheduling.dynPriority = IdlePriority;
        idle->scheduling.isPinned = true;
        idle->scheduling.lock.Unlock();

        localSched->idleThread = idle;

        RemoteStatus(MyCoreId())->scheduler = &*localSched;
    }

    void Private::OnPassiveRunLevel()
    {
        auto& sched = *localSched;

        if (sched.switchPending.Exchange(false, sl::Acquire))
            Yield();
    }

    void Private::BeginWait()
    {
        AssertIpl(Ipl::Dpc);

        auto thread = GetCurrentThread();
        auto& data = thread->scheduling;

        data.lock.Lock();
        data.state = ThreadState::Waiting;
        data.sleepBegin = GetMonotonicTime();
        data.lock.Unlock();
    }

    void Private::EndWait(ThreadContext* thread)
    {
        AssertIpl(Ipl::Dpc);

        const auto sleepEnd = GetMonotonicTime();
        auto& data = thread->scheduling;

        data.lock.Lock();
        data.sleepTime = sleepEnd.epoch - data.sleepBegin.epoch;
        ReassesThread(thread);
        data.state = ThreadState::Standby;
        data.lock.Unlock();

        EnqueueThread(thread);
    }
}
