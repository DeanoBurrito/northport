#include <scheduling/Scheduler.h>
#include <devices/SystemClock.h>
#include <arch/x86_64/Tss.h>
#include <Algorithms.h>
#include <Log.h>
#include <Locks.h>

#define THREAD_ALL_STARTING_CAPACITY 128

namespace Kernel::Scheduling
{
    void IdleMain(void*)
    {
        while (1)
            CPU::Halt();
    }

    using RealStartType = void (*)(sl::NativePtr);
    void KernelThreadStartWrapper(sl::NativePtr realStart, sl::NativePtr arg)
    {
        RealStartType startPtr = (RealStartType)realStart.raw;
        startPtr(arg);
        Thread::Current()->Exit();
    }
    
    void Scheduler::SaveContext(Thread* thread, StoredRegisters* regs)
    {
        thread->kernelStack = CurrentTss()->rsp0;
        thread->programStack = regs;
    }

    StoredRegisters* Scheduler::LoadContext(Thread* thread)
    {
        CurrentTss()->rsp0 = thread->kernelStack.raw;
        thread->parent->vmm.MakeActive();

        return thread->programStack.As<StoredRegisters>();
    }

    Scheduler* globalScheduler;
    Scheduler* Scheduler::Global()
    { 
        if (!globalScheduler)
            globalScheduler = new Scheduler();
        return globalScheduler; 
    }

    void Scheduler::Init(size_t baseProcessorId)
    {
        sl::SpinlockRelease(&lock);

        suspended = false;
        allThreads.EnsureCapacity(THREAD_ALL_STARTING_CAPACITY);
        for (size_t i = 0; i < allThreads.Capacity(); i++)
            allThreads.EmplaceBack(); //default fill = nullptrs
        
        //create idle thread, processor status for the bsp
        ThreadGroup* idleGroup = CreateThreadGroup();
        idleGroup->Name() = "Idle Group";
        while (processorStatus.Size() <= baseProcessorId)
            processorStatus.EmplaceBack();

        const size_t idleId = CreateThread((NativeUInt)IdleMain, ThreadFlags::KernelMode, idleGroup)->id;
        processorStatus[baseProcessorId].currentThread = processorStatus[baseProcessorId].idleThread = allThreads[idleId];
        processorStatus[baseProcessorId].isIdling = true;
        processorStatus[baseProcessorId].idleThread->Start(nullptr);

        //create some utility threads we need
        CreateThread((size_t)CleanupThreadMain, ThreadFlags::KernelMode, idleGroup)->Start(&cleanupData);
        CreateThread((size_t)DeviceEventPump, ThreadFlags::KernelMode, idleGroup)->Start(nullptr);
        lastSelectedThread = *allThreads.Begin();
        
        Log("Scheduler initialized, waiting for next tick.", LogSeverity::Info);
    }

    void Scheduler::AddProcessor(size_t id)
    {
        sl::SpinlockAcquire(&lock);

        ThreadGroup* idleGroup = processorStatus[0].idleThread->parent;
        while (processorStatus.Size() <= id)
            processorStatus.EmplaceBack();
        
        sl::SpinlockRelease(&lock);
        const size_t idleId = CreateThread((NativeUInt)IdleMain, ThreadFlags::KernelMode, idleGroup)->id;
        processorStatus[id].currentThread = processorStatus[id].idleThread = allThreads[idleId];
        processorStatus[id].isIdling = true;
        processorStatus[id].idleThread->Start(nullptr);
    }

    StoredRegisters* Scheduler::Tick(StoredRegisters* currentRegs)
    {
        sl::ScopedSpinlock schedLock(&lock);

        if (suspended)
            return currentRegs;

        Thread* current = Thread::Current();
        if (current != nullptr)
        {
            sl::ScopedSpinlock scopeLock(&current->lock);
            SaveContext(current, currentRegs);
        }

        Thread* next = nullptr;
        Thread* nextBehindIndex = nullptr;
        bool behindIndex = true;
        bool foundNext = false;
        const uint64_t tickTimeMs = Devices::GetUptime();

        for (auto it = allThreads.Begin(); it != allThreads.End(); ++it)
        {
            Thread* scan = *it;
            if (scan == nullptr)
                continue;

            //again, since we're iterating here, lets do wakeup checks on any sleeping threads
            if (scan->runState == ThreadState::Sleeping || scan->runState == ThreadState::SleepingForEvents)
            {
                if (scan->wakeTime <= tickTimeMs && scan->wakeTime != 0)
                    scan->runState = ThreadState::Running;
            }
            
            if (behindIndex)
            {
                //since we're iterating here, check what thread we would run if we looped around
                if (scan->runState == ThreadState::Running && 
                    !sl::EnumHasFlag(scan->flags, ThreadFlags::Executing) && 
                    nextBehindIndex == nullptr)
                    nextBehindIndex = scan;
                
                if (scan == lastSelectedThread)
                    behindIndex = false;
                continue;
            }

            if (foundNext) //anything below here is part of the search for the next thread to run
                continue;
            
            if (scan->runState == ThreadState::Running && !sl::EnumHasFlag(scan->flags, ThreadFlags::Executing))
            {
                next = scan;
                foundNext = true;
            }
        }

        SchedulerProcessorStatus& localCpuStatus = processorStatus[GetCoreLocal()->apicId];
        if (next == nullptr)
            next = nextBehindIndex; //loop around
        if (next == nullptr)
            next = localCpuStatus.idleThread; //nothing available to run, run idle thread
        lastSelectedThread = next;

        localCpuStatus.currentThread = next;
        localCpuStatus.isIdling = (localCpuStatus.currentThread == localCpuStatus.idleThread);
        
        if (current != nullptr)
        {
            sl::ScopedSpinlock currLock(&current->lock);
            current->flags = sl::EnumClearFlag(current->flags, ThreadFlags::Executing);
        }

        sl::ScopedSpinlock nextScopeLock(&next->lock);
        next->flags = sl::EnumSetFlag(next->flags, ThreadFlags::Executing);
        GetCoreLocal()->ptrs[CoreLocalIndices::CurrentThread] = next;
        return LoadContext(next);
    }

    void Scheduler::Yield()
    {
        //NOTE: this is hardcoded to the scheduler tick interrupt
        asm("int $0x22"); 
    }

    void Scheduler::Suspend(bool suspendScheduling)
    { suspended = suspendScheduling; }

    ThreadGroup* Scheduler::CreateThreadGroup()
    {
        ThreadGroup* group = new ThreadGroup();
        
        group->id = idAllocator.Alloc();
        group->vmm.Init();
        group->userStackBitmap.Resize(16);

        threadGroups.Append(group);
        return group;
    }

    ThreadGroup* Scheduler::GetThreadGroup(size_t id) const
    {
        auto foundIterator = sl::FindIf(threadGroups.Begin(), threadGroups.End(), 
            [&](auto iterator) 
            {
                if ((**iterator).id == id)
                    return true;
                return false;
            });

        if (foundIterator != threadGroups.End())
            return *foundIterator;
        return nullptr;
    }

    Thread* Scheduler::CreateThread(sl::NativePtr entryAddress, ThreadFlags flags, ThreadGroup* parent)
    {
        const bool isKernelThread = sl::EnumHasFlag(flags, ThreadFlags::KernelMode);
        
        InterruptLock intLock;
        sl::ScopedSpinlock scopeLock(&lock);
        
        if (parent == nullptr)
            parent = CreateThreadGroup();

        Thread* thread = new Thread();
        thread->id = idAllocator.Alloc();
        thread->flags = sl::EnumClearFlag(flags, ThreadFlags::Executing); //ensure executing flag is cleared, otherwise itll never run
        thread->runState = ThreadState::PendingStart;
        thread->parent = parent;
        
        parent->threads.PushBack(thread);
        allThreads[thread->id] = thread;

        thread->programStack = isKernelThread ? parent->AllocKernelStack() : parent->AllocUserStack();
        if (isKernelThread)
            thread->kernelStack = thread->programStack;
        else
            thread->kernelStack = parent->AllocKernelStack();

        sl::NativePtr stackAccess = thread->kernelStack;

        //setup dummy frame base and return address
        sl::StackPush<NativeUInt>(stackAccess, 0);
        sl::StackPush<NativeUInt>(stackAccess, 0);

        //setup iret frame
        sl::StackPush<NativeUInt>(stackAccess, isKernelThread ? GDT_ENTRY_RING_0_DATA : (GDT_ENTRY_RING_3_DATA | 3));
        sl::StackPush<NativeUInt>(stackAccess, thread->programStack.raw);
        sl::StackPush<NativeUInt>(stackAccess, 0x202); //interrupts set, everything else cleared
        sl::StackPush<NativeUInt>(stackAccess, isKernelThread ? GDT_ENTRY_RING_0_CODE  : (GDT_ENTRY_RING_3_CODE | 3));
        sl::StackPush<NativeUInt>(stackAccess, isKernelThread ? (NativeUInt)KernelThreadStartWrapper : entryAddress.raw);

        //push the rest of a stored registers frame (EC, vector, then 16 registers)
        for (size_t i = 0; i < 18; i++)
            sl::StackPush<NativeUInt>(stackAccess, 0);

        StoredRegisters* regs = stackAccess.As<StoredRegisters>();
        regs->rdi = entryAddress.raw;

        thread->programStack = stackAccess; //we'll stack with the kernel stack

        return thread;
    }

    void Scheduler::RemoveThread(size_t id)
    {
        InterruptLock intLock;
        
        Thread* thread = GetThread(id);
        if (thread == nullptr)
        {
            Logf("Could not remove thread 0x%lx, no thread with that id.", LogSeverity::Error, id);
            return;
        }

        if (thread->runState != ThreadState::PendingCleanup)
        {
            Logf("Could not remove thread 0x%lx, runstate != PendingCleanup.", LogSeverity::Error, id);
            return;
        }

        //TODO: we should check the thread isnt currently executing somewhere, and wait until that quantum has finished.

        sl::SpinlockAcquire(&lock);
        allThreads[id] = nullptr;
        idAllocator.Free(id);
        sl::SpinlockRelease(&lock);

        ThreadGroup* parent = thread->parent;
        sl::SpinlockAcquire(&parent->lock);

        //free the program and kernel stacks
        // if (!sl::EnumHasFlag(thread->flags, ThreadFlags::KernelMode))
        //     parent->FreeKernelStack(thread->programStack); //TODO: lets not clean up the resources immediately, and instead let the cleanup thread do that.
        // else
        // {
        //     parent->FreeUserStack(thread->programStack);
        //     parent->FreeKernelStack(thread->kernelStack);
        // }

        //if this is the last thread in the process, we'll want to free the process too.
        if (parent->threads.Size() == 1 && parent->threads[0]->id == id)
        {
            //we're freeing the parent too
            parent->threads.Clear();

            sl::ScopedSpinlock scopeLock(&lock);
            idAllocator.Free(parent->id);
            auto foundIterator = threadGroups.Find(parent);
            if (foundIterator != threadGroups.End())
                threadGroups.Remove(foundIterator);
            sl::SpinlockRelease(&lock);
            
            sl::ScopedSpinlock dataLock(&cleanupData.lock);
            cleanupData.processes.PushBack(parent);
        }
        else
        {
            //otherwise we just remove the thread and move on
            bool removed = false;
            for (size_t i = 0; i < parent->threads.Size(); i++)
            {
                if (parent->threads[i]->id == id)
                {
                    parent->threads.Erase(i); 
                    removed = true;
                    break;
                }
            }

            if (!removed)
                Logf("Could not find thread %lu in parent %lu.", LogSeverity::Error, id, parent->id);
            sl::SpinlockRelease(&parent->lock);
        }

        if (thread == Thread::Current())
            GetCoreLocal()->ptrs[CoreLocalIndices::CurrentThread] = nullptr;
        delete thread;
        thread = nullptr;
    }

    Thread* Scheduler::GetThread(size_t id) const
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
        return nullptr;
    }
}
