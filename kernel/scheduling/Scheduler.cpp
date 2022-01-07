#include <scheduling/Scheduler.h>
#include <memory/PhysicalMemory.h>
#include <arch/x86_64/Gdt.h>
#include <Algorithms.h>
#include <Platform.h>
#include <Memory.h>
#include <Log.h>

namespace Kernel::Scheduling
{
    using RealStartType = void (*)(sl::NativePtr);
    void ThreadStartWrapper(sl::NativePtr realStart, sl::NativePtr arg)
    {
        RealStartType startPtr = (RealStartType)realStart.raw;
        startPtr(arg);
        Thread::Current()->Exit();
    }

    void IdleMain()
    {
        while (1)
            asm("hlt");
    }
    
    Scheduler* Scheduler::Local()
    { return GetCoreLocal()->ptrs[CoreLocalIndices::Scheduler].As<Scheduler>(); }

    void Scheduler::Init()
    {
        SpinlockRelease(&lock);
        idGen.Alloc(); //id=0 means to drop the current thread, we dont want to accidentally allocate it
        suspended = false;
        
        Thread* idleThread = CreateThread((size_t)IdleMain, ThreadFlags::KernelMode);
        idleThread->Start(nullptr);
        //we're dropping this pointer, but this is okay as the idle thread should ALWAYS be present.
        (void)idleThread;
    }

    StoredRegisters* Scheduler::SelectNextThread(StoredRegisters* currentRegs)
    {
        if (suspended)
            return currentRegs;
        
        ScopedSpinlock scopeLock(&lock);

        Thread* currentThread = GetCurrentThread();
        if (currentThread && currentId != 0)
        {
            //we can safely save registers
            currentThread->regs = currentRegs;
        }

        //thread selection: round robin
        bool selectNextValid = false;
        Thread* nextThread = threads[0]; //first thread is always the idle thread - if selection fails, we'll run it
        for (size_t i = 0; i < threads.Size(); i++)
        {
            Thread* scan = threads[i];
            //prime next valid thread to be selected
            if (scan->threadId == currentId)
            {
                selectNextValid = true;
                continue;
            }
            else if (!selectNextValid)
                continue;

            //filter any threads that we dont care about
            if (scan->runState != ThreadState::Running)
                continue;

            //actual selection
            if (selectNextValid)
            {
                nextThread = scan;
                break;
            }
        }
        //if we reached the end, and were ready to select, select the first thread after the idle one
        if (nextThread->threadId == 1 && selectNextValid && threads.Size() > 1 && threads[1]->runState == ThreadState::Running)
            nextThread = threads[1];

        currentId = nextThread->threadId;
        return nextThread->regs;
    }

    [[noreturn]]
    void Scheduler::Yield()
    {
        //NOTE: this is hardcoded, the real definition is in Platform.h
        asm("int $0x22");
        __builtin_unreachable();
    }

    void Scheduler::Suspend(bool suspend)
    { suspended = suspend; }

    Thread* Scheduler::CreateThread(sl::NativePtr entryAddr, ThreadFlags flags)
    {
        ScopedSpinlock scopeLock(&lock);

        sl::NativePtr stack = Memory::PMM::Global()->AllocPage();
        Memory::PageTableManager::Local()->MapMemory(EnsureHigherHalfAddr(stack.ptr), stack, Memory::MemoryMapFlag::AllowWrites);

        Thread* thread = new Thread();
        thread->threadId = idGen.Alloc();
        thread->flags = flags;
        thread->runState = ThreadState::PendingStart;

        //we're forming a downwards stack, make sure the pointer starts at the top of the allocated space
        stack.raw += PAGE_FRAME_SIZE;
        stack.ptr = EnsureHigherHalfAddr(stack.ptr);
        
        //setup stack: this entirely dependant on cpu arch and the calling convention. We're using sys v abi.
        sl::StackPush<NativeUInt>(stack, 0); //dummy rbp and return address
        sl::StackPush<NativeUInt>(stack, 0);
        NativeUInt realStackStart = stack.raw;

        //All these stack interactions are setting up a StoredRegisters instance for the scheduler to use when it loads this thread for the first time.
        if (!sl::EnumHasFlag(flags, ThreadFlags::KernelMode))
            sl::StackPush<NativeUInt>(stack, GDT_USER_DATA);
        else
            sl::StackPush<NativeUInt>(stack, GDT_KERNEL_DATA);
        sl::StackPush<NativeUInt>(stack, realStackStart);
        sl::StackPush<NativeUInt>(stack, 0x202); //interrupts enabled, everything else set to default
        if (!sl::EnumHasFlag(flags, ThreadFlags::KernelMode))
            sl::StackPush<NativeUInt>(stack, GDT_USER_CODE);
        else
            sl::StackPush<NativeUInt>(stack, GDT_KERNEL_CODE);
        sl::StackPush<NativeUInt>(stack, (NativeUInt)ThreadStartWrapper);

        for (size_t i = 0; i < 6; i++) //push fake vector num, error code and then rax,rbx,rcx,rdx
            sl::StackPush<NativeUInt>(stack, 0); //dummy vector and error code

        sl::StackPush<NativeUInt>(stack, 0); //rsi: we'll overwrite this value later with the main function arg
        sl::StackPush<NativeUInt>(stack, entryAddr.raw); //rdi: first arg of wrapper, main func addr

        for (size_t i = 0; i < 10; i++) //push rbp,rsp, r8-r15 (all zeroed)
            sl::StackPush<NativeUInt>(stack, 0);

        thread->regs = stack.As<StoredRegisters>();

        threads.PushBack(thread);
        return thread;
    }

    void Scheduler::RemoveThread(size_t id)
    {
        ScopedSpinlock scopeLock(&lock);
    }

    Thread* Scheduler::GetCurrentThread()
    {
        if (currentId == 0)
            return nullptr;
        
        auto it = sl::FindIf(threads.Begin(), threads.End(), 
            [&](auto iterator){
                if ((*iterator)->threadId == currentId)
                    return true;
                return false;
            });
        
        if (it != threads.End())
            return *it;
        else
            return nullptr;
    }
}
