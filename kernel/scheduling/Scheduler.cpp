#include <scheduling/Scheduler.h>
#include <memory/PhysicalMemory.h>
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

    void Scheduler::Init(size_t coreCount)
    {
        sl::SpinlockRelease(&lock);

        suspended = false;
        realPressure = 0;
        allThreads.EnsureCapacity(THREAD_ALL_STARTING_CAPACITY);
        for (size_t i = 0; i < allThreads.Capacity(); i++)
            allThreads.EmplaceBack(); //default fill = nullptrs
        
        //create idle thread (1 per core)
        ThreadGroup* idleGroup = CreateThreadGroup();
        for (size_t i = 0; i < coreCount; i++)
        {
            size_t idleId = CreateThread((NativeUInt)IdleMain, ThreadFlags::KernelMode, idleGroup)->id;
            idleThreads.PushBack(allThreads[idleId]);
            idleThreads.Back()->Start(nullptr);
        }

        lastSelectedThread = *allThreads.Begin();
        
        Log("Scheduler initialized, waiting for next tick.", LogSeverity::Info);
    }

    StoredRegisters* Scheduler::Tick(StoredRegisters* currentRegs)
    {
        sl::ScopedSpinlock schedLock(&lock);

        Thread* current = Thread::Current();
        if (current != nullptr)
        {
            sl::ScopedSpinlock scopeLock(&current->lock);
            SaveContext(current, currentRegs);
        }

        Thread* next = nullptr;
        Thread* nextBehindIndex = nullptr;
        bool behindIndex = true;
        for (auto it = allThreads.Begin(); it != allThreads.End(); ++it)
        {
            Thread* scan = *it;
            if (scan == nullptr)
                continue;
            
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

            if (scan->runState == ThreadState::Running && !sl::EnumHasFlag(scan->flags, ThreadFlags::Executing))
            {
                next = scan;
                break;
            }
        }

        if (next == nullptr)
            next = nextBehindIndex; //loop around
        if (next == nullptr)
            next = idleThreads[GetCoreLocal()->apicId]; //nothing available to run, run idle thread
        lastSelectedThread = next;
        
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

    [[noreturn]]
    void Scheduler::Yield()
    {
        //NOTE: this is hardcoded to the scheduler tick interrupt
        asm("int $0x22"); 
        __builtin_unreachable();
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

        return group;
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
        thread->kernelStack = kernelStack.raw + PAGE_FRAME_SIZE;

        return thread;
    }

    void Scheduler::RemoveThread(size_t id)
    {
        Log("RemoveThread() not implemented TODO", LogSeverity::Error);
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
