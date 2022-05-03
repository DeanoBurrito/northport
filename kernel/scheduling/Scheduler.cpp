#include <scheduling/Scheduler.h>
#include <memory/PhysicalMemory.h>
#include <devices/SystemClock.h>
#include <arch/x86_64/Tss.h>
#include <Platform.h>
#include <Algorithms.h>
#include <Memory.h>
#include <Log.h>
#include <Locks.h>

#define THREAD_ALL_STARTING_CAPACITY 128
#define THREAD_STACK_PAGES 2

namespace Kernel::Scheduling
{
    void IdleMain(void* arg)
    {
        (void)arg;
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
        thread->parent->vmm.PageTables().MakeActive();

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
        realPressure = 0;
        allThreads.EnsureCapacity(THREAD_ALL_STARTING_CAPACITY);
        for (size_t i = 0; i < allThreads.Capacity(); i++)
            allThreads.EmplaceBack(); //default fill = nullptrs
        
        //create processor-specific structs
        ThreadGroup* idleGroup = CreateThreadGroup();
        while (processorStatus.Size() <= baseProcessorId)
            processorStatus.EmplaceBack();

        const size_t idleId = CreateThread((NativeUInt)IdleMain, ThreadFlags::KernelMode, idleGroup)->id;
        processorStatus[baseProcessorId].currentThread = processorStatus[baseProcessorId].idleThread = allThreads[idleId];
        processorStatus[baseProcessorId].isIdling = true;
        processorStatus[baseProcessorId].idleThread->Start(nullptr);

        lastSelectedThread = *allThreads.Begin();
        CreateThread((size_t)CleanupThreadMain, ThreadFlags::KernelMode, idleGroup)->Start(&cleanupData);
        CreateThread((size_t)DeviceEventPump, ThreadFlags::KernelMode, idleGroup)->Start(nullptr);
        
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

    size_t Scheduler::GetPressure() const
    {
        return realPressure;
    }

    ThreadGroup* Scheduler::CreateThreadGroup()
    {
        ThreadGroup* group = new ThreadGroup();
        
        group->id = idAllocator.Alloc();
        group->vmm.Init();
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

        //TODO: implement PMM::AllocPages, and alloc multiple here
        NativeUInt threadStackPhysBase = (NativeUInt)Memory::PMM::Global()->AllocPage();
        sl::NativePtr threadStack = EnsureHigherHalfAddr(threadStackPhysBase);
        if (sl::EnumHasFlag(flags, ThreadFlags::KernelMode))
            thread->programStackRange = { Memory::MemoryMapFlags::None, threadStack.raw, PAGE_FRAME_SIZE };
        else
            thread->programStackRange = { Memory::MemoryMapFlags::None, EnsureLowerHalfAddr(threadStack.raw), PAGE_FRAME_SIZE };
        threadStack.raw += PAGE_FRAME_SIZE;

        //setup dummy frame base and return address
        sl::StackPush<NativeUInt>(threadStack, 0);
        sl::StackPush<NativeUInt>(threadStack, 0);
        NativeUInt realStackStart = threadStack.raw;

        //setup iret frame
        if (sl::EnumHasFlag(flags, ThreadFlags::KernelMode))
        {
            sl::StackPush<NativeUInt>(threadStack, GDT_ENTRY_RING_0_DATA);
            sl::StackPush<NativeUInt>(threadStack, realStackStart);
            sl::StackPush<NativeUInt>(threadStack, 0x202); //interrupts set, everything else cleared
            sl::StackPush<NativeUInt>(threadStack, GDT_ENTRY_RING_0_CODE);
            sl::StackPush<NativeUInt>(threadStack, (NativeUInt)KernelThreadStartWrapper);
        }
        else
        {
            //NOTE: we are setting RPL (lowest 2 bits) of the selectors to 3.
            sl::StackPush<NativeUInt>(threadStack, GDT_ENTRY_RING_3_DATA | 3);
            sl::StackPush<NativeUInt>(threadStack, EnsureLowerHalfAddr(realStackStart));
            sl::StackPush<NativeUInt>(threadStack, 0x202);
            sl::StackPush<NativeUInt>(threadStack, GDT_ENTRY_RING_3_CODE | 3);
            sl::StackPush<NativeUInt>(threadStack, entryAddress.raw); //TODO: copy start program stub to userspace memory: UserThreadStartWrapper
        }

        //push the rest of a stored registers frame (EC, vector, then 16 registers)
        for (size_t i = 0; i < 18; i++)
            sl::StackPush<NativeUInt>(threadStack, 0);

        StoredRegisters* regs = threadStack.As<StoredRegisters>();
        regs->rdi = entryAddress.raw;

        if (!sl::EnumHasFlag(flags, ThreadFlags::KernelMode))
        {
            threadStack.raw -= vmaHighAddr; //user mode thread, use lower half address
            parent->VMM()->PageTables().MapMemory(threadStackPhysBase, threadStackPhysBase, Memory::MemoryMapFlags::AllowWrites | Memory::MemoryMapFlags::UserAccessible);
        }
        thread->programStack = threadStack;

        sl::NativePtr kernelStack = EnsureHigherHalfAddr(Memory::PMM::Global()->AllocPage());
        thread->kernelStackRange = { Memory::MemoryMapFlags::None, kernelStack.raw, PAGE_FRAME_SIZE };
        thread->kernelStack = kernelStack.raw + PAGE_FRAME_SIZE;

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

        //free the program and kernel stacks for this thread
        Memory::PMM::Global()->FreePages(EnsureLowerHalfAddr(thread->kernelStackRange.base), thread->kernelStackRange.length / PAGE_FRAME_SIZE);
        Memory::PMM::Global()->FreePages(EnsureLowerHalfAddr(thread->programStackRange.base), thread->programStackRange.length / PAGE_FRAME_SIZE);

        ThreadGroup* parent = thread->parent;
        sl::SpinlockAcquire(&parent->lock);

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
