#include <tasking/Scheduler.h>
#include <tasking/Clock.h>
#include <boot/CommonInit.h>
#include <debug/Log.h>
#include <interrupts/Ipi.h>

namespace Npk::Tasking
{
    constexpr size_t IdleStackSize = 0x1000;

    inline static Engine& LocalEngine()
    { return *static_cast<Engine*>(CoreLocal()[LocalPtr::Scheduler]); }

    void IdleMain(void*)
    {
        Halt();
    }

    static Thread* GetRunnableThread(WorkQueue& queue)
    {
        sl::ScopedLock scopeLock(queue.lock);

        while (true)
        {
            Thread* queueHead = queue.threads.PopFront();
            if (queueHead == nullptr)
                return nullptr; //queue is empty

            queue.depth--; //we pulled something from the queue, can we run it?
            if (queueHead->State() != ThreadState::Queued)
                continue;

            return queueHead;
        }

        return nullptr;
    }

    void Scheduler::DoReschedule(void*)
    {
        const auto prevRl = EnsureRunLevel(RunLevel::Dpc);

        Engine& engine = LocalEngine();
        DequeueClockEvent(&engine.rescheduleClockEvent);

        //reset ticket counts if needed
        if (engine.sharedTickets == 0)
        {
            engine.sharedTickets = engine.cluster->sharedQueue.depth;
            engine.localTickets = engine.localQueue.depth * engine.cluster->engineCount;
        }

        Thread* nextThread = nullptr;
        //try a thread from the local queue first
        if (engine.localTickets > 0)
        {
            nextThread = GetRunnableThread(engine.localQueue);
            if (nextThread != nullptr)
                engine.localTickets--;
        }
        //otherwise try the shared queue from the cluster
        if (nextThread == nullptr)
        {
            nextThread = GetRunnableThread(engine.cluster->sharedQueue);
            if (nextThread != nullptr)
                engine.sharedTickets--;
        }
        //no threads available, switch to the idle thread
        if (nextThread == nullptr)
            nextThread = engine.idleThread;

        nextThread->schedLock.Lock();
        nextThread->state = ThreadState::Running;
        nextThread->engineOrQueue.engineId = engine.id;
        nextThread->schedLock.Unlock();

        //TODO: decide on time until next timed reschedule
        engine.rescheduleClockEvent.duration = 10_ms;
        //QueueClockEvent(&engine.rescheduleClockEvent);

        engine.pendingThread = nextThread;
        engine.flags.Clear(EngineFlag::ReschedulePending);
        if (prevRl.HasValue())
            LowerRunLevel(*prevRl);
    }

    void Scheduler::LateInit()
    {
        Thread::Create(Process::Kernel().Id(), InitThread, nullptr)->Start(nullptr);
        //TODO: spawn cleanup thread
    }

    Scheduler globalScheduler;
    Scheduler& Scheduler::Global()
    { return globalScheduler; }

    void Scheduler::Init()
    {}

    void Scheduler::AddEngine()
    {
        Engine* engine = nullptr;
        enginesLock.WriterLock();
        //find a cluster for this engine to be a part of
        for (auto it = clusters.Begin(); it != clusters.End(); it = it->next)
        {
            for (size_t i = 0; i < EnginesPerCluster; i++)
            {
                if (it->engines[i].id != -1ul)
                    continue;
                engine = &it->engines[i];
                engine->cluster = it;
                it->engineCount++;
                break;
            }
            if (engine != nullptr)
                break;
        }

        //all clusters full (or none exist), make a new one
        if (engine == nullptr)
        {
            EngineCluster* cluster = new EngineCluster();
            clusters.PushBack(cluster);
            for (size_t i = 0; i < EnginesPerCluster; i++)
                cluster->engines[i].id = -1ul;
            cluster->engineCount = 1;
            engine = &cluster->engines[0];
            engine->cluster = cluster;
        }

        const bool doLateInit = engines.Size() == 0;
        engines.EmplaceAt(CoreLocal().id, engine);
        enginesLock.WriterUnlock();

        engine->localTickets = engine->sharedTickets = 0;
        engine->id = CoreLocal().id;
        engine->extRegsOwner = 0;
        engine->flags = 0;
        engine->pendingThread = nullptr;

        engine->rescheduleDpc.data.function = DoReschedule;
        engine->rescheduleClockEvent.dpc = &engine->rescheduleDpc;
        engine->rescheduleClockEvent.callbackCore = CoreLocal().id;
        CoreLocal()[LocalPtr::Scheduler] = engine;

        auto maybeIdle = ProgramManager::Global().CreateThread(
            Process::Kernel().Id(), IdleMain, nullptr, NoCoreAffinity, IdleStackSize);
        ASSERT_(maybeIdle.HasValue());
        engine->idleThread = Thread::Get(*maybeIdle);
        ASSERT_(engine->idleThread != nullptr);

        Log("Added scheduler engine: %zu", LogLevel::Info, engine->id);

        if (doLateInit)
            LateInit();
    }

    [[noreturn]]
    void Scheduler::StartEngine()
    {
        ASSERT_(CoreLocal().runLevel == RunLevel::Dpc);
        Log("Starting engine %zu, idleThread=%zu", LogLevel::Verbose, LocalEngine().id,
            LocalEngine().idleThread->id);

        QueueReschedule();
        DisableInterrupts();
        LowerRunLevel(RunLevel::Normal); //run pending DPCs (like rescheduling)
        ASSERT_UNREACHABLE();
    }

    void Scheduler::Yield(bool noSave)
    {
        ASSERT_(CoreLocal().runLevel == RunLevel::Normal);

        TrapFrame** prevFrameStorage = &Thread::Current().frame;
        QueueReschedule();

        SwitchFrame(noSave ? nullptr : prevFrameStorage, Thread::Current().frame);
        if (noSave)
            ASSERT_UNREACHABLE();
    }

    bool Scheduler::SwitchPending()
    { return LocalEngine().pendingThread != nullptr; }

    void Scheduler::DoPendingSwitch()
    {
        ASSERT_(CoreLocal().runLevel == RunLevel::Normal);
        auto& engine = LocalEngine();
        ASSERT_(engine.pendingThread != nullptr);

        sl::InterruptGuard intGuard;
        //put current thread back in queue if required
        Thread* current = static_cast<Thread*>(CoreLocal()[LocalPtr::Thread]);
        TrapFrame** prevFrame = current != nullptr ? &current->frame : nullptr;
        if (current != nullptr && current->id != engine.idleThread->id
            && current->state == ThreadState::Running)
        {
            WorkQueue* queue = &engine.cluster->sharedQueue;
            if (current->affinity != NoCoreAffinity)
            {
                ASSERT_(current->affinity == CoreLocal().id);
                queue = &engine.localQueue;
            }

            current->schedLock.Lock();
            current->state = ThreadState::Queued;
            current->engineOrQueue.queue = queue;
            current->schedLock.Unlock();

            queue->lock.Lock();
            queue->threads.PushBack(current);
            queue->depth++;
            queue->lock.Unlock();
        }

        //make pending thread the current one and switch
        TrapFrame* nextFrame = engine.pendingThread->frame;
        CoreLocal()[LocalPtr::Thread] = engine.pendingThread;
        engine.pendingThread = nullptr;

        SwitchFrame(prevFrame, nextFrame);
    }

    void Scheduler::EnqueueThread(Thread* t)
    {
        VALIDATE(t != nullptr,, "Cannot enqueue null thread");
        VALIDATE(t->state == ThreadState::Ready,, "Thread not in ready state");

        WorkQueue* queue = nullptr;
        if (t->affinity != NoCoreAffinity)
        {
            //thread has requested a specific engine to be queued on
            enginesLock.ReaderLock();
            for (size_t i = 0; i < engines.Size(); i++)
            {
                if (engines[i]->id != t->affinity)
                    continue;
                queue = &engines[i]->localQueue;
                break;
            }
            enginesLock.ReaderUnlock();
        }
        else
        {
            //otherwise queue it on the least worked cluster
            size_t queueSize = -1ul;
            EngineCluster* selected = nullptr;

            enginesLock.ReaderLock();
            for (auto it = clusters.Begin(); it != clusters.End(); it = it->next)
            {
                if (it->sharedQueue.depth < queueSize)
                {
                    queueSize = it->sharedQueue.depth;
                    selected = it; //TODO: enabling feature requests from threads (or power suggestions)
                }
            }
            enginesLock.ReaderUnlock();

            if (selected != nullptr)
                queue = &selected->sharedQueue;
        }
        VALIDATE(queue != nullptr,, "Failed to find suitable engine for thread");

        t->schedLock.Lock();
        t->state = ThreadState::Queued;
        t->engineOrQueue.queue = queue;
        t->schedLock.Unlock();

        queue->lock.Lock();
        queue->threads.PushBack(t);
        queue->lock.Unlock();
        queue->depth++;
    }

    static void RemoteDequeue(void* thread)
    { Scheduler::Global().DequeueThread(static_cast<Thread*>(thread)); }

    void Scheduler::DequeueThread(Thread* t)
    {
        VALIDATE_(t != nullptr, );
        VALIDATE_(t->state == ThreadState::Running || t->state == ThreadState::Queued, );

        auto& engine = LocalEngine();
        const auto prevRunlevel = EnsureRunLevel(RunLevel::Apc);

        t->schedLock.Lock();
        const ThreadState prevState = t->state;
        t->state = ThreadState::Ready;

        if (prevState == ThreadState::Running)
        {
            if (t->engineOrQueue.engineId == engine.id)
            {
                if (t == CoreLocal()[LocalPtr::Thread])
                    QueueReschedule();
                else
                    engine.pendingThread = nullptr;
            }
            else
                Interrupts::SendIpiMail(t->engineOrQueue.engineId, RemoteDequeue, t);
        }
        else
        {
            sl::ScopedLock queueLock(t->engineOrQueue.queue->lock);
            ASSERT_(t->engineOrQueue.queue->threads.Remove(t));
            t->engineOrQueue.queue->depth--;
        }

        t->schedLock.Unlock();

        if (prevRunlevel.HasValue())
            LowerRunLevel(*prevRunlevel);
    }

    void Scheduler::QueueReschedule()
    {
        auto& localEngine = LocalEngine();
        if (localEngine.flags.Set(EngineFlag::ReschedulePending))
            QueueDpc(&localEngine.rescheduleDpc);
    }

    void Scheduler::SwapExtendedRegs()
    {
        ASSERT_UNREACHABLE();
    }
}
