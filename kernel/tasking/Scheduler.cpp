#include <tasking/Scheduler.h>
#include <tasking/Process.h>
#include <tasking/Clock.h>
#include <memory/Vmm.h>
#include <debug/Log.h>

namespace Npk::Tasking
{
    constexpr size_t DefaultStackSize = 0x4000;

    Scheduler globalScheduler;
    Scheduler& Scheduler::Global()
    { return globalScheduler; }

    void Scheduler::Init()
    {
        nextPid = nextTid = 2; //id = 0 is reserved for 'null', id = 1 is the idle threads
        idleProcess = CreateProcess(nullptr);
    }

    void RescheduleDpc(void*)
    {
        Scheduler::Global().Reschedule();
        Scheduler::Global().DpcExit();
    }

    void TimerCallback(void*)
    {
        if (!CoreLocalAvailable())
            return;
        Scheduler::Global().QueueDpc(RescheduleDpc, nullptr);
    }

    void IdleMain(void*)
    { 
        while (true)
        {
            Debug::DrainBalloon();
            Wfi();
        }
        ASSERT_UNREACHABLE();
    }

    void Scheduler::RegisterCore(bool beginScheduling)
    {
        SchedulerCore* core = cores.EmplaceAt(CoreLocal().id, new SchedulerCore());
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
        InitTrapFrame(core->idleThread->frame, core->idleThread->stack.base + core->idleThread->stack.length, (uintptr_t)IdleMain, nullptr, false);

        auto dpcStack = VMM::Kernel().Alloc(DefaultStackSize, 1, VmFlags::Anon | VmFlags::Write);
        ASSERT(dpcStack, "No DPC stack for core.");
        core->dpcStack = dpcStack->Top();
        core->dpcStack -= sizeof(TrapFrame);
        core->dpcFrame = reinterpret_cast<TrapFrame*>(core->dpcStack);
        core->dpcFinished = true;

        CoreLocal().schedThread = core->idleThread;
        
        core->state = CoreState::Available;
        QueueClockEvent(10'000'000, nullptr, TimerCallback, true);
        Log("Scheduler registered core %lu.", LogLevel::Verbose, CoreLocal().id);

        if (!beginScheduling)
            return;
        CoreLocal().runLevel = RunLevel::Dispatch;
        core->lock.Unlock();
        RunNextFrame();
    }

    Process* Scheduler::CreateProcess(void* environment)
    {
        const size_t id = __atomic_fetch_add(&nextPid, 1, __ATOMIC_RELAXED);
        Process* proc = new Process();
        proc->id = id;

        processesLock.Lock();
        processes.Emplace(id, proc);
        processesLock.Unlock();

        (void)environment;//TODO: process environments
        return proc;
    }

    Thread* Scheduler::CreateThread(ThreadMain entry, void* arg, Process* parent, size_t coreAffinity)
    {
        if (parent == nullptr)
            parent = CreateProcess(nullptr);
        
        const size_t tid = __atomic_fetch_add(&nextTid, 1, __ATOMIC_RELAXED);
        threadsLock.Lock();
        Thread* thread = threadLookup.EmplaceAt(tid, new Thread());
        thread->id = tid;
        threadsLock.Unlock();

        thread->parent = parent;
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

        size_t smallestCount = (size_t)-1;
        size_t smallestIndex = 0;
        if (coreAffinity == -1ul)
        {
            for (size_t i = 0; i < cores.Size(); i++)
            {
                if (cores[i] == nullptr || cores[i]->state != CoreState::Available)
                    continue;
                if (cores[i]->threadCount >= smallestCount)
                    continue;
                
                smallestCount = cores[i]->threadCount;
                smallestIndex = i;
            }
            if (smallestIndex == (size_t)-1)
                smallestIndex = CoreLocal().id;
        }
        else
            smallestIndex = coreAffinity;
        
        cores[smallestIndex]->lock.Lock();
        cores[smallestIndex]->threads.PushBack(thread);
        cores[smallestIndex]->threadCount++;
        cores[smallestIndex]->lock.Unlock();
        
        thread->state = ThreadState::Runnable;
        Log("Thread %lu added to core %lu", LogLevel::Debug, tid, smallestIndex);
        return thread;
    }

    void DpcQueue(void (*function)(void* arg), void* arg)
    { Scheduler::Global().QueueDpc(function, arg); }

    void Scheduler::QueueDpc(ThreadMain function, void* arg)
    {
        ASSERT(CoreLocal().runLevel == RunLevel::IntHandler, "Run level too low.");
        SchedulerCore& core = *cores[CoreLocal().id];

        sl::ScopedLock scopeLock(core.lock);
        core.dpcs.EmplaceBack(function, arg);
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

        for (auto it = core.threads.Begin(); it != core.threads.End(); ++it)
        {
            Thread* scan = *it;
            if (loopedNext == nullptr && scan->state == ThreadState::Runnable)
            {
                loopedNext = scan;
                if (current == nullptr)
                    break;
            }

            if (current != nullptr && scan->id == current->id)
                passedCurrent = true;
            
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
        else if (CoreLocal().schedThread != nullptr)
            static_cast<Thread*>(CoreLocal().schedThread)->frame = current;
    }

    void Scheduler::RunNextFrame()
    {
        if (!CoreLocalAvailable() || CoreLocal().id >= cores.Size() || cores[CoreLocal().id] == nullptr)
            return; //we're super early in init, missing critical data so just return.
        
        DisableInterrupts();
        ASSERT(CoreLocal().runLevel != RunLevel::Normal, "Run level too low");

        SchedulerCore& core = *cores[CoreLocal().id];
        core.lock.Lock();
        if (!core.dpcFinished)
        {
            //resume an interrupted dpc
            CoreLocal().runLevel = RunLevel::Dispatch;
            core.lock.Unlock();
            ExecuteTrapFrame(core.dpcFrame);
            __builtin_unreachable();
        }
        
        if (core.dpcs.Size() > 0)
        {
            //run the next dpc
            const DeferredCall dpc = core.dpcs.PopFront();
            InitTrapFrame(core.dpcFrame, core.dpcStack, (uintptr_t)dpc.function, dpc.arg, false);

            CoreLocal().runLevel = RunLevel::Dispatch;
            core.dpcFinished = false;
            core.lock.Unlock();
            ExecuteTrapFrame(core.dpcFrame);
            __builtin_unreachable();
        }

        //no dpcs, resume the current thread.
        TrapFrame* frame = static_cast<Thread*>(CoreLocal().schedThread)->frame;
        ASSERT(frame->ec == 0xC0DE, "bad error code");

        CoreLocal().runLevel = RunLevel::Normal;
        core.lock.Unlock();
        ExecuteTrapFrame(frame);
        __builtin_unreachable();
    }
}
