#include <tasking/Scheduler.h>
#include <tasking/Clock.h>
#include <boot/CommonInit.h>
#include <debug/Log.h>

namespace Npk::Tasking
{
    constexpr size_t IdleStackSize = 0x1000;

    inline static Engine& LocalEngine()
    { return *static_cast<Engine*>(CoreLocal()[LocalPtr::Scheduler]); }

    void IdleMain(void*)
    {
        Halt();
    }

    void DoReschedule(void* arg)
    {
        auto engine = static_cast<Engine*>(arg);
        //TODO: add clock event for next reschedule

        if (CoreLocal()[LocalPtr::Thread] != nullptr)
        {
            Thread* current = static_cast<Thread*>(CoreLocal()[LocalPtr::Thread]);
            WorkQueue* const queue = current->GetAffinity() == engine->id 
                ? &engine->localQueue 
                : &engine->cluster->sharedQueue;

            queue->lock.Lock();
            queue->threads.PushBack(current);
            queue->depth++;
            queue->lock.Unlock();
        }

        /* Decide which queue we should pull work from. This is still round robin, but
         * split over 2 work queues (the local one for threads with an explicitly set
         * affinity, and the cluster for general scheduling).
         * To be (kind of, but not really) fair to all threads we have a 'cycle' length
         * for the number of jobs this core runs. On a single-queue round-robin
         * scheduler the cycle length is the number of threads in the queue, here
         * I've naturally done something a bit more convoluted.
         * The idea is to execute the number of items in the shared queue once,
         * and then execute the items in the local queue a number of times.
         * For example if there are 3 engines in the cluster, the shared queue has 3x
         * as many opportunities to run as the local queue, so in response we run
         * through the local queue 3 times.
         * This idea has it's own problems, but its a first pass at this idea.
         */
        if (engine->queueCycleDepth == 0)
        {
            engine->queueCycleDepth = engine->cluster->sharedQueue.depth;
            engine->queueCycleDepth += engine->localQueue.depth * engine->cluster->engineCount;
        }

        WorkQueue* queue = nullptr;
        if (engine->queueCycleDepth > engine->cluster->sharedQueue.depth &&
            engine->localQueue.depth > 0)
            queue = &engine->localQueue;
        else
            queue = &engine->cluster->sharedQueue;
        engine->queueCycleDepth--;
        ASSERT_(queue != nullptr);

        queue->lock.Lock();
        Thread* nextThread = queue->threads.PopFront();
        if (nextThread != nullptr)
            queue->depth--;
        queue->lock.Unlock();

        if (nextThread == nullptr)
            nextThread = engine->idleThread;

        CoreLocal()[LocalPtr::Thread] = nextThread;
        engine->flags.Clear(EngineFlag::ReschedulePending);
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

        engine->id = CoreLocal().id;
        engine->extRegsOwner = 0;
        engine->flags = 0;
        engine->rescheduleDpc.data.function = DoReschedule;
        engine->rescheduleDpc.data.arg = engine;
        CoreLocal()[LocalPtr::Scheduler] = engine;

        auto maybeIdle = ProgramManager::Global().CreateThread(
            Process::Kernel().Id(), IdleMain, nullptr, NoAffinity, IdleStackSize);
        ASSERT_(maybeIdle.HasValue());
        engine->idleThread = Thread::Get(*maybeIdle);
        ASSERT_(engine->idleThread != nullptr);

        Log("Added scheduler engine: %lu", LogLevel::Info, engine->id);

        if (doLateInit)
            LateInit();
    }

    [[noreturn]]
    void Scheduler::StartEngine()
    {
        ASSERT_(CoreLocal().runLevel == RunLevel::Dpc);
        Log("Starting engine %lu, idleThread=%lu", LogLevel::Verbose, LocalEngine().id,
            LocalEngine().idleThread->id);

        QueueReschedule();
        DisableInterrupts();
        LowerRunLevel(RunLevel::Normal); //run pending DPCs (like rescheduling)
        SwitchFrame(nullptr, Thread::Current().frame);
        ASSERT_UNREACHABLE();
    }

    bool Scheduler::Suspend(bool yes)
    {
        if (yes)
            return !LocalEngine().flags.Set(EngineFlag::Clutch);
        else
            return LocalEngine().flags.Clear(EngineFlag::Clutch);
    }

    void Scheduler::EnqueueThread(Thread* t)
    {
        VALIDATE(t != nullptr,, "Cannot enqueue null thread");
        VALIDATE(t->state == ThreadState::Ready,, "Thread not in ready state");

        WorkQueue* queue = nullptr;
        if (t->affinity != NoAffinity)
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

        queue->lock.Lock();
        queue->threads.PushBack(t);
        queue->lock.Unlock();
        queue->depth++;
    }

    void Scheduler::DequeueThread(Thread* t)
    {
        ASSERT_UNREACHABLE();
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
