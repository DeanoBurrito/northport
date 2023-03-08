#include <tasking/Scheduler.h>
#include <boot/CommonInit.h>
#include <interrupts/Ipi.h>
#include <tasking/Process.h>
#include <tasking/ServiceThreads.h>
#include <tasking/Clock.h>
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

    void Scheduler::QueuePush(SchedulerCore& core, Thread* item) const
    {
        //TODO: would be nice to go lockless here oneday :)
        sl::ScopedLock scopeLock(core.queue.lock);

        item->next = nullptr;

        if (core.queue.tail == nullptr)
            core.queue.head = item;
        else
            core.queue.tail->next = item;
        core.queue.tail = item;

        ++core.queue.size;
    }

    Thread* Scheduler::QueuePop(SchedulerCore& core) const
    {
        sl::ScopedLock scopeLock(core.queue.lock);

        Thread* found = core.queue.head;
        if (found != nullptr)
        {
            core.queue.head = found->next;
            --core.queue.size;
        }
        else
        {
            core.queue.head = core.queue.tail = nullptr;
            core.queue.size = 0;
        }
        return found;
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
        //this function runs inside the timer interrupt handler
        Scheduler::Global().QueueReschedule();
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
        core->dpcLock.Lock();
        core->dpcs.EmplaceBack();
        core->dpcs.PopBack();
        core->dpcLock.Unlock();

        DisableInterrupts();
        core->queue.lock.Lock();
        core->queue.head = core->queue.tail = nullptr; //clear the runqueue
        core->suspendScheduling = false;
        core->reschedulePending = false;

        CoreLocal().schedThread = core->idleThread;
        CoreLocal().schedData = core;
        core->queue.lock.Unlock(); //this lock also doubles as a memory barrier
        
        coresListLock.Lock();
        cores.PushBack(core);
        coresListLock.Unlock();

        Log("Scheduler registered core %lu.", LogLevel::Verbose, CoreLocal().id);

        if (registeredCores.FetchAdd(1) == 0)
            LateInit(); //first core to register triggers some late init

        if (initThread != nullptr)
        {
            initThread->coreAffinity = CoreLocal().id;
            initThread->Start();
        }

        QueueClockEvent(10'000'000, nullptr, QueueRescheduleDpc, true);
        Yield(false);
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
                if (core->queue.size >= smallestCount)
                    continue;
                
                smallestCount = core->queue.size;
                selectedCore = core;
            }
        }

        ASSERT(selectedCore != nullptr, "Failed to allocate processor.");
        QueuePush(*selectedCore, t);
        t->activeCore = selectedCore->coreId;
    }

    void Scheduler::DequeueThread(size_t id)
    {
        ASSERT(id <= threadLookup.Size(), "Invalid thread id");
        ASSERT(threadLookup[id] != nullptr, "Invalid thread id");

        Thread* t = threadLookup[id];
        InterruptGuard intGuard;
        t->lock.Lock();
        ASSERT(t->state == ThreadState::Runnable, "Thread is not runnable");
        t->state = ThreadState::Ready;
        t->lock.Unlock();

        //find core thread is queued on
        SchedulerCore* core = GetCore(t->activeCore);
        ASSERT(core != nullptr, "Thread not queued");

        Thread* prev = nullptr;
        sl::ScopedLock queueLock(core->queue.lock);
        for (Thread* scan = core->queue.head; scan != nullptr; prev = scan, scan = scan->next)
        {
            if (scan->id != id)
                continue;
            
            if (prev != nullptr)
                prev->next = scan->next;
            else
                core->queue.head = scan->next;
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
        core.dpcLock.Lock();
        core.dpcs.EmplaceBack(function, arg);
        core.dpcLock.Unlock();

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
        if (!CoreLocalAvailable() || CoreLocal().schedData == nullptr)
            return;
        
        InterruptGuard intGuard;
        SchedulerCore& core = *static_cast<SchedulerCore*>(CoreLocal().schedData);

        TrapFrame** current = nullptr;
        //there's no point in returning to a DPC or interrupt handler that yielded.
        willReturn = willReturn && (CoreLocal().runLevel == RunLevel::Normal);
        if (willReturn && CoreLocal().schedThread != nullptr)
            current = &(static_cast<Thread*>(CoreLocal().schedThread)->frame);

        Thread* activeThread = static_cast<Thread*>(CoreLocal().schedThread);
        ASSERT(activeThread != nullptr, "Current thread is null.");
        //we may be yielding because the current thread was dequeued: in which case
        //we try queue a reschedule (assuming one isnt already pending).
        if (activeThread->state != ThreadState::Runnable)
            QueueReschedule();

        //select the next trap frame to load and execute, depending on priority:
        // - if we interrupted a DPC, resume it so it can finish,
        // - otherwise execute the next queued dpc,
        // - and otherwise we just run the current thread if there's one,
        // - fall back to running the idle thread if all that fails.
        TrapFrame* next = nullptr;
        if (!core.dpcFinished)
        {
            CoreLocal().runLevel = RunLevel::Dispatch;
            next = core.dpcFrame; //run the unfinished DPC
        }
        else if (core.dpcs.Size() > 0)
        {
            CoreLocal().runLevel = RunLevel::Dispatch;

            //run the next queued DPC
            const DeferredCall dpc = core.dpcs.PopFront();
            InitTrapFrame(core.dpcFrame, core.dpcStack, (uintptr_t)dpc.function, dpc.arg, false);
            core.dpcFinished = false;
            next = core.dpcFrame;
        }
        else
        {
            Thread* thread = static_cast<Thread*>(CoreLocal().schedThread);
            next = thread->frame;
            CoreLocal().runLevel = RunLevel::Normal;

            thread->parent->vmm.MakeActive();
        }

        if (current != nullptr && (*current == next))
            return; //dont bother saving/reloading if its the same task, just return.

        SwitchFrame(current, next);
    }

    void Scheduler::Reschedule()
    {
        ASSERT(CoreLocal().runLevel == RunLevel::Dispatch, "Bad run level");
        
        SchedulerCore& core = *static_cast<SchedulerCore*>(CoreLocal().schedData);
        if (core.suspendScheduling)
            return;

        //decide what to do with the current thread: requeue or drop it
        Thread* current = static_cast<Thread*>(CoreLocal().schedThread);
        ASSERT(current != nullptr, "Current thread is null?");

        if (current->state != ThreadState::Runnable || current->id < 2)
        {
            //drop the thread from the run queue:
            //- if its not in a runnable state, it doesnt belong here.
            //- id 0 means 'no thread' and is a reserved value, id 1 is the idle
            //  thread for this core.
            current->activeCore = 0;
        }
        else
            QueuePush(core, current);

        //get the next runnable thread. A thread is always runnable when added to the queue,
        //but this can change while it waits in the queue.
        Thread* next = QueuePop(core);
        while (next != nullptr && next->state != ThreadState::Runnable)
            next = QueuePop(core);

        //work stealing: if the queue was empty, try pop from another core's queue.
        if (next == nullptr && registeredCores > 1)
        {
            //select a random processor as an offset from the current one.
            size_t targetOffset = sl::Max(NextRand() % registeredCores, 1ul);
            auto scan = cores.Begin();
            while ((**scan).coreId != CoreLocal().id)
                ++scan; //find our starting position in the list
            
            ASSERT(scan != cores.End(), "Current core is not registered");
            while (targetOffset > 0)
            {
                ++scan;
                if (scan == cores.End())
                    scan = cores.Begin();
                --targetOffset;
            }

            //we found our target core, try to pop from their runqueue.
            SchedulerCore& target = **scan;
            next = QueuePop(target); //TODO: we don't honour core affinities here.
            next->activeCore = core.coreId; //thread has migrated processors
        }

        //we failed to get another thread to run, just idle.
        if (next == nullptr)
            next = core.idleThread;

        CoreLocal().schedThread = next;
        core.reschedulePending = false;
    }

    void Scheduler::QueueReschedule()
    {
        InterruptGuard intGuard;
        SchedulerCore& core = *static_cast<SchedulerCore*>(CoreLocal().schedData);
        
        if (core.reschedulePending)
            return;
        core.dpcs.EmplaceBack(RescheduleDpc, nullptr);
        core.reschedulePending = true;
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

    void Scheduler::SavePrevFrame(TrapFrame* current, RunLevel prevRunLevel)
    {
        if (!CoreLocalAvailable() || CoreLocal().schedData == nullptr)
            return;
        
        ASSERT(CoreLocal().runLevel == RunLevel::IntHandler, "Bad run level");
        SchedulerCore& core = *static_cast<SchedulerCore*>(CoreLocal().schedData);

        if (prevRunLevel == RunLevel::Dispatch)
            core.dpcFrame = current;
        else if (CoreLocal().schedThread != nullptr && prevRunLevel == RunLevel::Normal)
            static_cast<Thread*>(CoreLocal().schedThread)->frame = current;
    }
}
