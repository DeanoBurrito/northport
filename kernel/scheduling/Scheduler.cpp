#include <scheduling/Scheduler.h>
#include <devices/SystemClock.h>
#include <arch/x86_64/Tss.h>
#include <devices/LApic.h>
#include <Algorithms.h>
#include <Log.h>
#include <Locks.h>

namespace Kernel::Scheduling
{
    constexpr size_t UserStackBitmapInitCount = 16;
    constexpr size_t KernelStackBitmapInitCount = 16;
    constexpr size_t ProcessorIndexIdleThread = (size_t)-2;
    constexpr size_t ThreadCleanupAmnestyMs = 3;
    
    void IdleMain(void*)
    {
        while (true)
            CPU::Halt();
    }

    void Scheduler::SpawnUtilityThreads()
    {
        //TODO: do these really belong in the idle group?
        CreateThread((NativeUInt)CleanupThreadMain, ThreadFlags::KernelMode, idleGroup)->Start(&cleanupData);
        CreateThread((NativeUInt)DeviceEventPump, ThreadFlags::KernelMode, idleGroup)->Start(nullptr);
    }

    bool Scheduler::EnqueueOnCore(Thread* thread, size_t core)
    {
        //-1 meaning we don't care what core's queue we add it too: find the least-busy
        if (core == (size_t)-1)
        {
            size_t lowestCount = (size_t)-1;
            for (size_t i = 0; i < processors.Size(); i++)
            {
                if (processors[i].isPlaceholder)
                    continue;
                if (processors[i].threads.Size() < lowestCount)
                {
                    lowestCount = processors[i].threads.Size();
                    core = i;
                }
            }

            if (core == (size_t)-1) //we didn't find an appropriate core (somehow)
                return false;
        }

        if (core >= processors.Size())
            return false;
        
        ProcessorStatus* proc = &processors[core];
        if (proc->isPlaceholder)
            return false;
        
        InterruptLock intLock;
        sl::ScopedSpinlock procLock(&proc->lock);

        proc->threads.PushBack(thread);
        thread->core = core;

        return true;
    }

    bool Scheduler::DequeueFromCore(Thread* thread, size_t core)
    {
        if (core >= processors.Size())
            return false;
        
        ProcessorStatus* proc = &processors[core];
        if (proc->isPlaceholder)
            return false;

        InterruptLock intLock;
        sl::ScopedSpinlock procLock(&proc->lock);

        for (size_t i = 0; i < proc->threads.Size(); i++)
        {
            if (proc->threads[i] == thread)
            {
                //this only removes it from the queue, it does not delete the thread.
                proc->threads.Erase(i);
                return true;
            }
        }

        return false;
    }

    Scheduler* globalScheduler = nullptr;
    Scheduler* Scheduler::Global()
    {
        if (globalScheduler == nullptr)
            globalScheduler = new Scheduler();
        return globalScheduler;
    }

    void Scheduler::Init()
    {
        sl::SpinlockRelease(&globalLock);
        sl::SpinlockRelease(&cleanupData.lock);

        //consume id=0 from both, since it may accidentally get used as a 'null' value
        //even if we don't support that.
        groupIdAlloc.Alloc();
        threadIdAlloc.Alloc();
        suspended = false;

        idleGroup = CreateThreadGroup();
        idleGroup->Name() = "IdleGroup";
    }

    void Scheduler::AddProcessor(size_t id)
    {
        Thread* const idleThread = CreateThread((uintptr_t)IdleMain, ThreadFlags::KernelMode, idleGroup, ProcessorIndexIdleThread);
        idleThread->Name() = "IdleThread";
        idleThread->core = id;
        const bool spawnUtilThreads = processors.Empty();
        
        {
            InterruptLock intLock;
            sl::ScopedSpinlock scopeLock(&globalLock);
            
            processors.EnsureCapacity(id);
            while (processors.Size() <= id)
                processors.EmplaceBack();
            
            processors[id].idleThread = idleThread;
            processors[id].isPlaceholder = false;
            sl::SpinlockRelease(&processors[id].lock);
        }
        if (spawnUtilThreads)
            SpawnUtilityThreads();

        Logf("Scheduler added processor: id=%u", LogSeverity::Verbose, id);
    }

    StoredRegisters* Scheduler::Tick(StoredRegisters* regs)
    {
        if (suspended)
            return regs;
        
        ProcessorStatus* proc = &processors[CoreLocal()->id];
        sl::ScopedSpinlock procLock(&proc->lock);

        Thread* currentThread = Thread::Current();
        if (currentThread != nullptr && currentThread->runState != ThreadState::PendingCleanup)
        {
            //save the current thread's context
            sl::ScopedSpinlock threadLock(&currentThread->lock);
            currentThread->kernelStack = CurrentTss()->rsp0;
            currentThread->programStack = regs;
            currentThread->flags = sl::EnumClearFlag(currentThread->flags, ThreadFlags::Executing);
        }

        Thread* next = nullptr;
        Thread* nextBehindIndex = nullptr;
        bool behindIndex = true;
        bool foundNext = false;
        const uint64_t tickTimeMs = Devices::GetUptime();

        /*
            This selection algorithm is pretty basic, I tried to as much as possible while only traversing
            all the threads once.
            - We select the next thread to run (looking forward).
            - We also select the next thread to run if we've reached the end of the list (wrap around).
            - Check for any sleeping threads, and if their wake timers have experied.
        */
        for (auto it = proc->threads.Begin(); it != proc->threads.End(); ++it)
        {
            Thread* scan = *it;
            if (scan == nullptr)
                continue;
            
            if (scan->runState == ThreadState::Sleeping || scan->runState == ThreadState::SleepingForEvents)
            {
                if (scan->wakeTime <= tickTimeMs && scan->wakeTime != 0)
                    scan->runState = ThreadState::Running;
            }

            if (behindIndex)
            {
                if (nextBehindIndex == nullptr && scan->runState == ThreadState::Running)
                    nextBehindIndex = scan;
                
                if (scan == proc->lastSelected)
                    behindIndex = false;
                continue;
            }

            if (foundNext)
                continue;
            
            if (scan->runState == ThreadState::Running && !sl::EnumHasFlag(scan->flags, ThreadFlags::Executing))
            {
                next = scan;
                foundNext = true;
            }
        }

        //check if we've reached the end of the queue, and wrap around.
        //or if we need to use the idle thread. There is always something to run.
        if (next == nullptr)
            next = nextBehindIndex == nullptr ? proc->idleThread : nextBehindIndex;

        sl::ScopedSpinlock nextThreadLock(&next->lock);
        proc->lastSelected = next;
        CoreLocal()->ptrs[CoreLocalIndices::CurrentThread] = next;
        next->flags = sl::EnumSetFlag(next->flags, ThreadFlags::Executing);
        
        //load the new context
        CurrentTss()->rsp0 = next->kernelStack.raw;
        
        //TrapExit checks if we try to load '1' as the stack, which is obviously invalid.
        //In this case it'll pop a new cr3/satp value from the stack, load it and then
        //load the new stack pointer. This lets us do scheduling normally, but keep our
        //isolated kernel stacks. We also don't need a temporary global stack for this.
        proc->targetStack.magic = 1;
        proc->targetStack.translationAddr = next->parent->vmm.GetTla().raw;
        proc->targetStack.stackAddr = next->programStack.raw;
        return sl::NativePtr(&proc->targetStack).As<StoredRegisters>();
    }

    void Scheduler::Yield()
    {
        asm volatile("int $" MACRO_STR(INT_VECTOR_SCHEDULER_TICK));
    }

    void Scheduler::YieldCore(size_t core)
    {
        if (core == CoreLocal()->id)
            Yield();
        else //there can be only one! Yield!
            Devices::LApic::Local()->SendIpi(core, INT_VECTOR_SCHEDULER_TICK);
    }

    void Scheduler::Suspend(bool yes)
    {
        suspended = yes;
    }

    ThreadGroup* Scheduler::CreateThreadGroup()
    {
        ThreadGroup* group = new ThreadGroup();
        group->vmm.Init();
        group->userStackBitmap.Resize(UserStackBitmapInitCount);
        group->kernelStackBitmap.Resize(KernelStackBitmapInitCount);

        InterruptLock intLock;
        sl::ScopedSpinlock scopeLock(&globalLock);
        group->id = groupIdAlloc.Alloc();
        
        threadGroups.EnsureCapacity(group->id);
        while (threadGroups.Size() <= group->id)
            threadGroups.EmplaceBack(nullptr);
        threadGroups[group->id] = group;

        return group;
    }

    void Scheduler::RemoveThreadGroup(size_t id)
    {
        auto maybeGroup = GetThreadGroup(id);
        if (!maybeGroup)
            return;

        ThreadGroup* group = *maybeGroup;
        sl::ScopedSpinlock groupLock(&group->lock);
        InterruptLock intLock;

        sl::SpinlockAcquire(&globalLock);
        threadGroups[group->id] = nullptr;
        groupIdAlloc.Free(group->id);
        sl::SpinlockRelease(&globalLock);

        //TODO: implement meeeee!
    }

    sl::Opt<ThreadGroup*> Scheduler::GetThreadGroup(size_t id) const
    {
        auto foundIterator = sl::FindIf(threadGroups.Begin(), threadGroups.End(), 
            [&](auto iterator) 
            {
                if (*iterator == nullptr)
                    return false;
                if ((**iterator).id == id)
                    return true;
                return false;
            });

        if (foundIterator != threadGroups.End())
            return *foundIterator;
        return {};
    }

    Thread* Scheduler::CreateThread(sl::NativePtr entryAddress, ThreadFlags flags, ThreadGroup* parent, size_t coreIndex)
    {
        const bool isKernelThread = sl::EnumHasFlag(flags, ThreadFlags::KernelMode);

        if (parent == nullptr)
            parent = CreateThreadGroup();
        
        Thread* thread = new Thread();
        thread->flags = sl::EnumClearFlag(flags, ThreadFlags::Executing);
        thread->runState = ThreadState::PendingStart;
        thread->parent = parent;

        thread->programStack = isKernelThread ? parent->AllocKernelStack() : parent->AllocUserStack();
        {
            //set up dummy return address and stack base (as per sys v abi).
            //Put these in a separate scope, because I dont want to accidentally use initStackAcccess later.
            sl::NativePtr initStackAccess = parent->vmm.GetPhysAddr(thread->programStack.raw - 1)->raw + 1;
            initStackAccess = EnsureHigherHalfAddr(initStackAccess.raw);
            sl::StackPush<NativeUInt>(initStackAccess, 0);
            sl::StackPush<NativeUInt>(initStackAccess, 0);
            thread->programStack.raw -= sizeof(NativeUInt) * 2;
        }
        thread->kernelStack = isKernelThread ? thread->programStack : parent->AllocKernelStack();
        thread->kernelStack.raw -= sizeof(StoredRegisters);

        sl::NativePtr stackAccess = parent->vmm.GetPhysAddr(thread->kernelStack.raw).Value().raw;
        stackAccess = EnsureHigherHalfAddr(stackAccess.raw);
        sl::memset(stackAccess.ptr, 0, sizeof(StoredRegisters));

        //setup iret frame
        StoredRegisters* initRegs = stackAccess.As<StoredRegisters>();
        initRegs->iret_cs = isKernelThread ? GDT_ENTRY_RING_0_CODE : (GDT_ENTRY_RING_3_CODE | 3);
        initRegs->iret_ss = isKernelThread ? GDT_ENTRY_RING_0_DATA : (GDT_ENTRY_RING_3_DATA | 3);
        initRegs->iret_flags = 0x202;
        initRegs->iret_rsp = thread->programStack.raw;
        initRegs->iret_rip = entryAddress.raw;

        thread->programStack = thread->kernelStack.raw;
        
        //we're entering the... *danger zone* B)
        InterruptLock intLock;
        {
            sl::ScopedSpinlock scopeLock(&globalLock);

            //add thread to the central list. This is for fast lookups using the id, not for executing.
            thread->id = threadIdAlloc.Alloc();
            allThreads.EnsureCapacity(thread->id);
            while (allThreads.Size() <= thread->id)
                allThreads.EmplaceBack(nullptr);
            allThreads[thread->id] = thread;
        }

        sl::SpinlockAcquire(&parent->lock);
        parent->threads.PushBack(thread);
        sl::SpinlockRelease(&parent->lock);

        //Idle threads are a special case, we dont want to assign them at all
        if (coreIndex == ProcessorIndexIdleThread)
            return thread;

        //try and assign this thread to the requested core
        if (!EnqueueOnCore(thread, coreIndex))
            Log("Failed to create thread: Could not find a core with the lowest workload.", LogSeverity::Fatal);
        
        return thread;
    }

    void Scheduler::RemoveThread(size_t id)
    {
        auto maybeThread = GetThread(id);
        if (!maybeThread)
            return;
        
        Thread* thread = *maybeThread;
        InterruptLock intLock;

        //ensure the thread is ready to be cleaned up. 
        if (thread->runState != ThreadState::PendingCleanup)
        {
            Log("Tried to remove thread from scheduler that was not in PendingCleanup state.", LogSeverity::Warning);
            thread->runState = ThreadState::PendingCleanup;
        }
        //if the thread is executing on another core, ask that core to reschedule
        if (thread->core != CoreLocal()->id && sl::EnumHasFlag(thread->flags, ThreadFlags::Executing))
            Scheduler::YieldCore(thread->core);

        //remove the thread from global state
        sl::SpinlockAcquire(&globalLock);
        allThreads[thread->id] = nullptr;
        threadIdAlloc.Free(thread->id);
        sl::SpinlockRelease(&globalLock);

        //remove thread from the core's work queue
        ProcessorStatus& proc = processors[thread->core];
        sl::SpinlockAcquire(&proc.lock);
        for (size_t i = 0; i < proc.threads.Size(); i++)
        {
            if (proc.threads[i]->id == id)
            {
                proc.threads.Erase(i);
                break;
            }
        }
        sl::SpinlockRelease(&proc.lock);

        /*
            All that remains is to free the thread's stacks and the thread object itself.
            A potential problem is we may free the active stack, so these actions are deferred, 
            and run as part of the cleanup thread.
            The thread's 'wakeTime' field is used to set the earliest time it's allowed to be
            freed, which we set to a few milliseconds from now (to give us time to reschedule).
        */
        thread->wakeTime = Devices::GetUptime() + ThreadCleanupAmnestyMs;
        sl::ScopedSpinlock cleanupLock(&cleanupData.lock);
        cleanupData.threads.PushBack(thread);
    }

    sl::Opt<Thread*> Scheduler::GetThread(size_t id) const
    {
        auto it = sl::FindIf(allThreads.Begin(), allThreads.End(), [=](auto it)
        {
            if (*it == nullptr)
                return false;
            if ((*it)->id == id)
                return true;
            return false;
        });

        if (it != allThreads.End())
            return *it;
        return {};
    }
}
