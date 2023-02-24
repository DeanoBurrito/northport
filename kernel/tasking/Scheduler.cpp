#include <tasking/Scheduler.h>
#include <boot/CommonInit.h>
#include <interrupts/Ipi.h>
#include <tasking/Process.h>
#include <tasking/ServiceThreads.h>
#include <tasking/Clock.h>
#include <memory/Vmm.h>
#include <debug/Log.h>

namespace Npk::Tasking
{
    constexpr size_t DefaultStackSize = 0x10000;

    CleanupData schedCleanupData;

    void Scheduler::LateInit()
    {
        //Init() runs before any cores are registered, LateInit() runs after at least 1 core
        //has been registered, and is suitible for things like creating service threads.

        CreateThread(SchedulerCleanupThreadMain, &schedCleanupData, idleProcess)->Start();
        CreateThread(InitThread, nullptr)->Start();
    }

    size_t Scheduler::NextRand()
    {
        ASSERT(CoreLocal().runLevel == RunLevel::Dispatch, "Run level too high.");
        sl::ScopedLock scopeLock(rngLock);

        return rng->Next();
    }

    SchedulerCore* Scheduler::GetCore(size_t id)
    {
        if (id == CoreLocal().id)
            return reinterpret_cast<SchedulerCore*>(CoreLocal().schedData);
        for (auto it = cores.Begin(); it != cores.End(); ++it)
        {
            if ((**it).coreId == id)
                return *it;
        }
        return nullptr;
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

    void QueueRescheduleDpc(void*)
    {
        Scheduler::Global().QueueDpc(RescheduleDpc, nullptr);
    }

    void IdleMain(void*)
    { 
        Halt();
        ASSERT_UNREACHABLE();
    }

    void Scheduler::RegisterCore(Thread* initThread)
    {
        SchedulerCore* core = new SchedulerCore();
        core->coreId = CoreLocal().id;

        //we create the idle thread manually, since it shouldn't be in the normal thread pool.
        core->idleThread = new Thread();
        core->idleThread->id = 1;
        core->idleThread->parent = idleProcess;
        core->idleThread->state = ThreadState::Dead;

        //setup the idle thread stack
        constexpr size_t IdleStackSize = PageSize;
        core->idleThread->stack.base = VMM::Kernel().Alloc(IdleStackSize, 1, VmFlags::Anon | VmFlags::Write)->base;
        core->idleThread->stack.length = IdleStackSize;
        core->idleThread->frame = reinterpret_cast<TrapFrame*>((core->idleThread->stack.base + IdleStackSize) - sizeof(TrapFrame));
        core->idleThread->frame = sl::AlignDown(core->idleThread->frame, sizeof(TrapFrame));
        InitTrapFrame(core->idleThread->frame, core->idleThread->stack.base + core->idleThread->stack.length, (uintptr_t)IdleMain, nullptr, false);

        //DPC (deferred procedure call) setup
        auto dpcStack = VMM::Kernel().Alloc(DefaultStackSize, 1, VmFlags::Anon | VmFlags::Write);
        ASSERT(dpcStack, "No DPC stack for core.");
        core->dpcStack = dpcStack->Top();
        core->dpcStack -= sizeof(TrapFrame);
        core->dpcFrame = reinterpret_cast<TrapFrame*>(core->dpcStack);
        core->dpcFinished = true;

        //Ensure the cache for DPC invocations is filled.
        core->dpcs.EmplaceBack();
        core->dpcs.PopBack();

        DisableInterrupts();
        core->lock.Lock();
        core->queue = core->queueTail = nullptr; //clear the work queue
        core->suspendScheduling = false; //enable scheduling by default

        CoreLocal().schedThread = core->idleThread;
        CoreLocal().schedData = core;
        core->lock.Unlock(); //lock is used as a memory barrier here.
        
        coresListLock.Lock();
        cores.PushBack(core);
        coresListLock.Unlock();

        Log("Scheduler registered core %lu.", LogLevel::Verbose, CoreLocal().id);

        if (__atomic_add_fetch(&activeCores, 1, __ATOMIC_RELAXED) == 1)
            LateInit();
        
        if (initThread != nullptr)
        {
            initThread->coreAffinity = CoreLocal().id;
            initThread->Start();
        }

        QueueClockEvent(10'000'000, nullptr, QueueRescheduleDpc, true);
        Yield();
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
        const bool isKernelThread = true; //TODO: userspace

        threadsLock.Lock();
        Thread* thread = threadLookup.EmplaceAt(tid, new Thread());
        thread->id = tid;
        threadsLock.Unlock();

        VMM& vmm = (isKernelThread ? VMM::Kernel() : thread->parent->vmm);
        auto maybeStack = vmm.Alloc(DefaultStackSize, isKernelThread ? 1 : 0, VmFlags::Anon | VmFlags::Write);
        ASSERT(maybeStack, "No thread stack");
        
        sl::ScopedLock threadLock(thread->lock);
        thread->stack.base = maybeStack->base;
        thread->stack.length = maybeStack->length;
        thread->frame = reinterpret_cast<TrapFrame*>((thread->stack.base + thread->stack.length) - sizeof(TrapFrame));

        if (isKernelThread)
            InitTrapFrame(thread->frame, thread->stack.base + thread->stack.length, (uintptr_t)entry, arg, false);
        else
        {
            // populate the frame in our own memory space, then copy it across.
            // this is arch-specific, InitTrapFrame is defined in arch/xyz/Platform.h
            TrapFrame frame;
            InitTrapFrame(&frame, thread->stack.base + thread->stack.length, (uintptr_t)entry, arg, false);
            parent->vmm.CopyIn((void*)thread->frame, &frame, sizeof(TrapFrame));
        }

        thread->parent = parent;
        thread->coreAffinity = coreAffinity;
        thread->activeCore = NoAffinity;
        thread->state = ThreadState::Ready;

        return thread;
    }

    void Scheduler::DestroyProcess(size_t id)
    {
        ASSERT(id <= processes.Size(), "Invalid process id");
        ASSERT(processes[id] != nullptr, "Invalid process id");
        Log("Process %lu is being destroyed", LogLevel::Debug, id);

        processesLock.Lock();
        Process* proc = processes[id];
        processes[id] = nullptr;
        processesLock.Unlock();

        schedCleanupData.lock.Lock();
        schedCleanupData.processes.PushBack(proc);
        schedCleanupData.lock.Unlock();
        
        //TODO: remove processes threads from active lists and core queues.
        ASSERT_UNREACHABLE();
    }

    void Scheduler::DestroyThread(size_t id, size_t errorCode)
    {
        ASSERT(id <= threadLookup.Size(), "Invalid thread id");
        ASSERT(threadLookup[id] != nullptr, "Invalid thread id");
        Log("Thread %lu exited with code %lu", LogLevel::Debug, id, errorCode);

        InterruptGuard guard; //TODO: should be SchedulerLock, rather interrupts
        // DequeueThread(id);

        threadsLock.Lock();
        Thread* t = threadLookup[id];
        threadLookup[id] = nullptr;
        //TODO: free tid (implement IdA)
        threadsLock.Unlock();

        t->lock.Lock();
        t->state = ThreadState::Dead;
        t->lock.Unlock();

        //TODO: if we're the last thread in a process, remove the whole process as well.

        schedCleanupData.lock.Lock();
        schedCleanupData.threads.PushBack(t);
        schedCleanupData.lock.Unlock();

        if (CoreLocal().schedThread == t)
            QueueDpc(RescheduleDpc);
    }

    void Scheduler::EnqueueThread(size_t id)
    {
        ASSERT(id <= threadLookup.Size(), "Invalid thread id");
        ASSERT(threadLookup[id] != nullptr, "Invalid thread id");

        Thread* t = threadLookup[id];
        sl::ScopedLock threadLock(t->lock);
        ASSERT(t->state == ThreadState::Ready, "Thread is not ready.");
        t->state = ThreadState::Runnable;

        SchedulerCore* selectedCore = nullptr;
        if (t->coreAffinity == CoreLocal().id) //preferred core is self, no list traversal needed
            selectedCore = static_cast<SchedulerCore*>(CoreLocal().schedData);
        else if (t->coreAffinity != NoAffinity)
        {
            //find preferred core
            for (auto i = cores.Begin(); i != cores.End(); ++i)
            {
                SchedulerCore* core = *i;
                if (core == nullptr || core->coreId != t->coreAffinity)
                    continue;
                selectedCore = core;
                break;
            }
        }

        if (selectedCore == nullptr)
        {
            //preferred core not found or n/a, select the least-worked core.
            size_t smallestCount = -1ul;
            for (auto it = cores.Begin(); it != cores.End(); ++it)
            {
                SchedulerCore* core = *it;
                if (core->threadCount >= smallestCount)
                    continue;
                
                smallestCount = core->threadCount;
                selectedCore = core;
            }
        }
        ASSERT(selectedCore != nullptr, "Failed to allocate processor.");

        sl::ScopedLock scopeLock(selectedCore->lock);
        t->next = nullptr;
        t->activeCore = selectedCore->coreId;
        //single list append
        if (selectedCore->queueTail == nullptr)
            selectedCore->queue = t;
        else
            selectedCore->queueTail->next = t;
        selectedCore->queueTail = t;
        
        selectedCore->threadCount.Add(1, sl::Relaxed);
    }

    void Scheduler::DequeueThread(size_t id)
    {
        ASSERT(id <= threadLookup.Size(), "Invalid thread id");
        ASSERT(threadLookup[id] != nullptr, "Invalid thread id");

        Thread* t = threadLookup[id];
        InterruptLock intGuard;
        t->lock.Lock();
        ASSERT(t->state == ThreadState::Runnable, "Thread is not runnable");
        t->state = ThreadState::Ready;
        t->lock.Unlock();

        //find core thread is queued on
        SchedulerCore* core = GetCore(t->activeCore);
        ASSERT(core != nullptr, "Thread not queued");
        sl::ScopedLock coreLock(core->lock);

        Thread* prev = nullptr;
        for (Thread* scan = core->queue; scan != nullptr; prev = scan, scan = scan->next)
        {
            if (scan->id != id)
                continue;
            
            if (prev != nullptr)
                prev->next = scan->next;
            else
                core->queue = scan->next;
            t->activeCore = NoAffinity;
            return;
        }

        /*  There are a few possible scenarios here:
            - thread was in a core's run-queue, in which case we already returned (see above).
            - thread is active on another core, then we trigger a reschedule via IPI.
            - thread is active on local core, then we do nothing as that would
            exit this function prematurely. This is the caller's responsibility to handle.
        */
        if (t->activeCore != CoreLocal().id)
            Interrupts::SendIpiMail(t->activeCore, QueueRescheduleDpc, nullptr);
    }

    sl::Opt<Thread*> Scheduler::GetThread(size_t id)
    {
        if (id >= threadLookup.Size() || id < 2)
            return {};
        if (threadLookup[id] == nullptr)
            return {};
        return threadLookup[id];
    }

    sl::Opt<Process*> Scheduler::GetProcess(size_t id)
    {
        if (id >= processes.Size() || id < 2)
            return {};
        if (processes[id] == nullptr)
            return {};
        return processes[id];
    }

    void DpcQueue(void (*function)(void* arg), void* arg)
    { Scheduler::Global().QueueDpc(function, arg); }

    void Scheduler::QueueDpc(ThreadMain function, void* arg)
    {
        SchedulerCore& core = *static_cast<SchedulerCore*>(CoreLocal().schedData);
        core.lock.Lock();
        core.dpcs.EmplaceBack(function, arg);
        core.lock.Unlock();

        if (CoreLocal().runLevel == RunLevel::Normal)
            Yield();
    }

    void DpcExit()
    { Scheduler::Global().DpcExit(); }

    void Scheduler::DpcExit()
    {
        ASSERT(CoreLocal().runLevel == RunLevel::Dispatch, "Bad run level.");

        SchedulerCore& core = *static_cast<SchedulerCore*>(CoreLocal().schedData);
        core.dpcFinished = true;
        Yield(false);
    }

    void Scheduler::Yield(bool willReturn)
    {
        //determine what runlevel we're at: then load trapframe location.
        //determine what run-level to target: find target trapframe (dpc > thread > idle).
        //switch trap frame

        DisableInterrupts();
        // //TODO: SaveCurrentContext(); so that we can return here if the thread isn't exiting
        RunNextFrame();
    }

    void Scheduler::Reschedule()
    {
        ASSERT(CoreLocal().runLevel == RunLevel::Dispatch, "Bad run level");
        
        SchedulerCore& core = *static_cast<SchedulerCore*>(CoreLocal().schedData);
        sl::ScopedLock coreLock(core.lock);
        if (core.suspendScheduling)
            return; //TODO: we should track a 'reschedule pending' flag, and check this in RunNextFrame.

        Thread* current = static_cast<Thread*>(CoreLocal().schedThread);
        if (current != nullptr && current->state == ThreadState::Runnable && current->id != 1)
        {
            //place current thread back into queue
            current->next = nullptr;
            if (core.queue == nullptr)
                core.queue = current;
            else
                core.queueTail->next = current;
            core.queueTail = current;
        }
        else if (current != nullptr && current->id != 1)
        {
            //drop current thread from queue
            core.threadCount--;
            current->activeCore = 0;
            current->state = ThreadState::Ready;
        }

        Thread* next = core.queue; //TODO: check thread is Runnable before selecting it
        if (next == nullptr && cores.Size() > 1)
        {
            //work stealing: pick a core at random, take from it's work queue.
            size_t targetOffset = sl::Max(NextRand() % cores.Size(), 1ul);
            auto scan = cores.Begin();
            while ((**scan).coreId != CoreLocal().id)
                ++scan;

            ASSERT(scan != cores.End(), "Core not registered.");
            while (targetOffset > 0)
            {
                ++scan;
                if (scan == cores.End())
                    scan = cores.Begin();
                targetOffset--;
            }

            SchedulerCore& target = **scan;
            sl::ScopedLock targetLock(target.lock);
            next = target.queue; //TODO: we dont honour thread/core affinities here
            if (next == nullptr)
                next = core.idleThread; //failed, target has nothing to steal
            else
            {
                //success, update target queue like it was our own.
                if (target.queue == target.queueTail)
                    target.queueTail = nullptr;
                target.queue = next->next;

                next->activeCore = CoreLocal().id;
                target.threadCount--;
            }
        }
        else if (next == nullptr && cores.Size() == 1)
            next = core.idleThread; //no work stealing on single core systems.
        else
        {
            if (core.queue == core.queueTail)
                core.queueTail = nullptr;
            core.queue = next->next;
        }
        
        CoreLocal().schedThread = next;
    }

    bool Scheduler::Suspend(bool yes)
    {
        InterruptGuard intGuard;
        
        SchedulerCore* core = reinterpret_cast<SchedulerCore*>(CoreLocal().schedData);
        const bool prevSuspend = core->suspendScheduling;
        core->suspendScheduling = yes;
        return prevSuspend;
        // return core->suspendScheduling.Exchange(yes);
    }

    void Scheduler::SaveCurrentFrame(TrapFrame* current, RunLevel prevRunLevel)
    {
        if (!CoreLocalAvailable() || CoreLocal().schedData == nullptr)
            return;
        
        SchedulerCore& core = *static_cast<SchedulerCore*>(CoreLocal().schedData);
        sl::ScopedLock scopeLock(core.lock);

        if (prevRunLevel == RunLevel::Dispatch)
            core.dpcFrame = current;
        else if (CoreLocal().schedThread != nullptr && prevRunLevel == RunLevel::Normal)
            static_cast<Thread*>(CoreLocal().schedThread)->frame = current;
    }

    void Scheduler::RunNextFrame()
    {
        if (!CoreLocalAvailable() || CoreLocal().schedData == nullptr)
            return; //we're super early in init, missing critical data so just return.

        auto RunNext = [](SchedulerCore& core, RunLevel level, TrapFrame* frame) 
        {
            CoreLocal().runLevel = level;
            core.lock.Unlock();
            ExecuteTrapFrame(frame);
            __builtin_unreachable();
        };
        
        DisableInterrupts();

        SchedulerCore& core = *static_cast<SchedulerCore*>(CoreLocal().schedData);
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
