#include <tasking/Scheduler.h>
#include <tasking/Process.h>
#include <tasking/ServiceThreads.h>
#include <tasking/Clock.h>
#include <memory/Vmm.h>
#include <debug/Log.h>

namespace Npk::Tasking
{
    constexpr size_t DefaultStackSize = 0x4000;

    CleanupData schedCleanupData;

    void Scheduler::LateInit()
    {
        //Init() runs before any cores are registered, LateInit() runs after at least 1 core
        //has been registered, and is suitible for things like creating service threads.

        CreateThread(SchedulerCleanupThreadMain, &schedCleanupData)->Start();
    }

    Scheduler globalScheduler;
    Scheduler& Scheduler::Global()
    { return globalScheduler; }

    void Scheduler::Init()
    {
        nextPid = nextTid = 2; //id = 0 is reserved for 'null', id = 1 is the idle threads
        idleProcess = CreateProcess();
    }

    void RescheduleDpc(void*)
    {
        Scheduler::Global().Reschedule();
        Scheduler::Global().DpcExit();
    }

    void TimerCallback(void*)
    {
        Scheduler::Global().QueueDpc(RescheduleDpc, nullptr);
    }

    void IdleMain(void*)
    { 
        Halt();
        ASSERT_UNREACHABLE();
    }

    void Scheduler::RegisterCore()
    {
        SchedulerCore* core = cores.EmplaceAt(CoreLocal().id, new SchedulerCore());
        InterruptGuard intGuard;
        sl::ScopedLock coreLock(core->lock);

        //we create the idle thread manually, since it shouldn't be in the normal thread pool.
        core->idleThread = new Thread();
        core->idleThread->id = 1;
        core->idleThread->parent = idleProcess;
        core->idleThread->state = ThreadState::Runnable;

        constexpr size_t IdleStackSize = PageSize;
        core->idleThread->stack.base = VMM::Kernel().Alloc(IdleStackSize, 1, VmFlags::Anon | VmFlags::Write)->base;
        core->idleThread->stack.length = IdleStackSize;
        core->idleThread->frame = reinterpret_cast<TrapFrame*>((core->idleThread->stack.base + IdleStackSize) - sizeof(TrapFrame));
        core->idleThread->frame = sl::AlignDown(core->idleThread->frame, sizeof(TrapFrame));
        InitTrapFrame(core->idleThread->frame, core->idleThread->stack.base + core->idleThread->stack.length, (uintptr_t)IdleMain, nullptr, false);

        auto dpcStack = VMM::Kernel().Alloc(DefaultStackSize, 1, VmFlags::Anon | VmFlags::Write);
        ASSERT(dpcStack, "No DPC stack for core.");
        core->dpcStack = dpcStack->Top();
        core->dpcStack -= sizeof(TrapFrame);
        core->dpcFrame = reinterpret_cast<TrapFrame*>(core->dpcStack);
        core->dpcFinished = true;

        CoreLocal().schedThread = core->idleThread;
        
        core->state = CoreState::Available;
        Log("Scheduler registered core %lu.", LogLevel::Verbose, CoreLocal().id);

        core->lock.Unlock();
        if (IsBsp())
            LateInit();

        QueueClockEvent(10'000'000, nullptr, TimerCallback, true);
        RunNextFrame();
        ASSERT_UNREACHABLE();
    }

    Process* Scheduler::CreateProcess()
    {
        const size_t id = __atomic_fetch_add(&nextPid, 1, __ATOMIC_RELAXED);
        Process* proc = new Process();
        proc->id = id;

        processesLock.Lock();
        processes.Emplace(id, proc);
        processesLock.Unlock();

        return proc;
    }

    Thread* Scheduler::CreateThread(ThreadMain entry, void* arg, Process* parent, size_t coreAffinity)
    {
        if (parent == nullptr)
            parent = CreateProcess();
        
        const size_t tid = __atomic_fetch_add(&nextTid, 1, __ATOMIC_RELAXED);

        threadsLock.Lock();
        Thread* thread = threadLookup.EmplaceAt(tid, new Thread());
        thread->id = tid;
        threadsLock.Unlock();

        auto maybeStack = parent->vmm.Alloc(DefaultStackSize, 1, VmFlags::Anon | VmFlags::Write);
        ASSERT(maybeStack, "No thread stack"); //TODO: critical junction here, do we fail or stall until memory is available?
        thread->stack.base = maybeStack->base;
        thread->stack.length = maybeStack->length;
        thread->frame = reinterpret_cast<TrapFrame*>((thread->stack.base + thread->stack.length) - sizeof(TrapFrame));

        //populate the frame in our own memory space, then copy it across.
        //this is arch-specific, InitTrapFrame is defined in arch/xyz/Platform.h
        TrapFrame frame;
        InitTrapFrame(&frame, thread->stack.base + thread->stack.length, (uintptr_t)entry, arg, false);
        parent->vmm.CopyIn((void*)thread->frame, &frame, sizeof(TrapFrame));

        thread->parent = parent;
        thread->coreAffinity = coreAffinity;
        thread->state = ThreadState::Ready;

        return thread;
    }

    void Scheduler::DestroyThread(size_t id, size_t errorCode)
    {
        ASSERT(id <= threadLookup.Size(), "Invalid thread id");
        ASSERT(threadLookup[id] != nullptr, "Invalid thread id");
        Log("Thread %lu exited with code %lu", LogLevel::Debug, id, errorCode);

        DisableInterrupts();
        threadsLock.Lock();
        Thread* t = threadLookup[id];
        threadLookup[id] = nullptr;
        t->state = ThreadState::Dead;
        //TODO: free tid (implement IdA)
        threadsLock.Unlock();

        schedCleanupData.lock.Lock();
        schedCleanupData.threads.PushBack(t);
        schedCleanupData.lock.Unlock();

        QueueDpc(RescheduleDpc);
    }

    void Scheduler::EnqueueThread(size_t id)
    {
        ASSERT(id <= threadLookup.Size(), "Invalid thread id");
        ASSERT(threadLookup[id] != nullptr, "Invalid thread id");

        Thread* t = threadLookup[id];
        ASSERT(t->state == ThreadState::Ready, "Thread is not ready.");
        t->state = ThreadState::Runnable;

        size_t affinity = t->coreAffinity;

        size_t smallestCount = -1ul;
        if (affinity == -1ul)
        {
            //core has no preferred core, find the least worked one.
            for (size_t i = 0; i < cores.Size(); i++)
            {
                if (cores[i] == nullptr || cores[i]->state != CoreState::Available)
                    continue;
                const size_t coreThreadCount = __atomic_load_n(&cores[i]->threadCount, __ATOMIC_RELAXED);
                if (coreThreadCount >= smallestCount)
                    continue;
                
                smallestCount = coreThreadCount;
                affinity = i;
            }
            ASSERT(affinity != -1ul, "Failed to allocate processor for thread.");
        }

        sl::ScopedLock scopeLock(cores[affinity]->lock);
        Thread* last = cores[affinity]->threads;
        if (last == nullptr)
            cores[affinity]->threads = t;
        else
        {
            while (last->next != nullptr)
                last = last->next;
            last->next = t;
        }
        cores[affinity]->threadCount++;
    }

    void DpcQueue(void (*function)(void* arg), void* arg)
    { Scheduler::Global().QueueDpc(function, arg); }

    void Scheduler::QueueDpc(ThreadMain function, void* arg)
    {
        if (!CoreLocalAvailable() || CoreLocal().id >= cores.Size() || cores[CoreLocal().id] == nullptr)
            return;
        
        SchedulerCore& core = *cores[CoreLocal().id];
        core.lock.Lock();
        core.dpcs.EmplaceBack(function, arg);
        core.lock.Unlock();

        if (CoreLocal().runLevel == RunLevel::Normal)
        {
            //run the dpc immediately, we'll return here later
            //SaveCurrentFrame()
            RunNextFrame();
        }
    }

    void DpcExit()
    { Scheduler::Global().DpcExit(); }

    void Scheduler::DpcExit()
    {
        ASSERT(CoreLocal().runLevel == RunLevel::Dispatch, "Bad run level.");
        cores[CoreLocal().id]->dpcFinished = true;
        RunNextFrame();
    }

    void Scheduler::Reschedule()
    {
        ASSERT(CoreLocal().runLevel != RunLevel::Normal, "Run level too low.");
        
        sl::ScopedLock coreLock(cores[CoreLocal().id]->lock);
        SchedulerCore& core = *cores[CoreLocal().id];
        if (core.state != CoreState::Available)
            return;

        Thread* current = static_cast<Thread*>(CoreLocal().schedThread);
        Thread* next = nullptr;
        Thread* loopedNext = nullptr;
        bool passedCurrent = false;

        for (Thread* scan = core.threads; scan != nullptr; scan = scan->next)
        {
            if (loopedNext == nullptr && scan->state == ThreadState::Runnable)
            {
                loopedNext = scan;
                if (current == nullptr)
                    break;
            }

            if (current != nullptr && scan->id == current->id)
            {
                passedCurrent = true;
                continue;
            }
            
            if (!passedCurrent)
                continue;
            if (scan->state != ThreadState::Runnable)
                continue;
            
            next = scan;
            break;
        }

        next = (next == nullptr) ? loopedNext : next;
        if (next == nullptr)
            next = core.idleThread; //TOOD: work stealing

        CoreLocal().schedThread = next;
    }

    void Scheduler::SaveCurrentFrame(TrapFrame* current, RunLevel prevRunLevel)
    {
        if (!CoreLocalAvailable() || CoreLocal().id >= cores.Size() || cores[CoreLocal().id] == nullptr)
            return;
        
        SchedulerCore& core = *cores[CoreLocal().id];
        sl::ScopedLock scopeLock(core.lock);

        if (prevRunLevel == RunLevel::Dispatch)
            core.dpcFrame = current;
        else if (CoreLocal().schedThread != nullptr && prevRunLevel == RunLevel::Normal)
            static_cast<Thread*>(CoreLocal().schedThread)->frame = current;
    }

    void Scheduler::RunNextFrame()
    {
        if (!CoreLocalAvailable() || CoreLocal().id >= cores.Size() || cores[CoreLocal().id] == nullptr)
            return; //we're super early in init, missing critical data so just return.

        auto RunNext = [](SchedulerCore& core, RunLevel level, TrapFrame* frame) 
        {
            CoreLocal().runLevel = level;
            core.lock.Unlock();
            ExecuteTrapFrame(frame);
            __builtin_unreachable();
        };
        
        DisableInterrupts();

        SchedulerCore& core = *cores[CoreLocal().id];
        core.lock.Lock();
        if (!core.dpcFinished)
            RunNext(core, RunLevel::Dispatch, core.dpcFrame);
        
        if (core.dpcs.Size() > 0)
        {
            const DeferredCall dpc = core.dpcs.PopFront();
            InitTrapFrame(core.dpcFrame, core.dpcStack, (uintptr_t)dpc.function, dpc.arg, false);
            core.dpcFinished = false;

            RunNext(core, RunLevel::Dispatch, core.dpcFrame);
        }

        //no dpcs, resume the current thread.
        Thread* target = static_cast<Thread*>(CoreLocal().schedThread);
        target->parent->vmm.MakeActive(); //TODO: we could be smarter about when we switch address spaces (ASIDS?)
        RunNext(core, RunLevel::Normal, target->frame);
    }
}
