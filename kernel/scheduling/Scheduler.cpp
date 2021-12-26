#include <scheduling/Scheduler.h>
#include <memory/PhysicalMemory.h>
#include <arch/x86_64/Gdt.h>
#include <Algorithms.h>
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
        
        Thread* idleThread = CreateThread((size_t)IdleMain, false);
        idleThread->Start(nullptr);
        //we're dropping this pointer, but this is okay as the idle thread should ALWAYS be present.
        (void)idleThread;
    }

    StoredRegisters* Scheduler::SelectNextThread(StoredRegisters* currentRegs)
    {
        ScopedSpinlock scopeLock(&lock);

        Thread* currentThread = GetCurrentThread();
        if (currentThread && currentId != 0) //thread id 0 means to drop the current thread
        {
            //we can safely save registers
            currentThread->regs = currentRegs;
        }

        auto it = threads.Begin();
        while (it != threads.End())
        {
            if (*it == currentThread && (*it)->runState == ThreadState::Running)
            {
                it++;
                break;
            }
            
            it++;
        }
        if (it == threads.End()) //cycle around if we hit the end
            it = threads.Begin();

        Thread* newThread = *it;
        currentId = newThread->threadId;

        return newThread->regs;
    }

    [[noreturn]]
    void Scheduler::Yield()
    {
        //NOTE: this is hardcoded, the real definition is in Platform.h
        asm("int $0x22");
        __builtin_unreachable();
    }

    Thread* Scheduler::CreateThread(sl::NativePtr entryAddr, bool userspace)
    {
        ScopedSpinlock scopeLock(&lock);

        sl::NativePtr stack = Memory::PMM::Global()->AllocPage();
        Memory::PageTableManager::Local()->MapMemory(stack, stack, Memory::MemoryMapFlag::AllowWrites);

        Thread* thread = new Thread();
        thread->threadId = idGen.Alloc();
        if (!userspace)
            thread->flags = sl::EnumSetFlag(thread->flags, ThreadFlags::KernelMode);
        thread->runState = ThreadState::PendingStart;
        
        //setup stack: this entirely dependant on cpu arch and the calling convention. We're using sys v abi.
        sl::StackPush<NativeUInt>(stack, 0); //dummy rbp and return address
        sl::StackPush<NativeUInt>(stack, 0);
        NativeUInt realStackStart = stack.raw;

        //All these stack interactions are setting up a StoredRegisters instance for the scheduler to use when it loads this thread for the first time.
        if (userspace)
            sl::StackPush<NativeUInt>(stack, GDT_USER_DATA);
        else
            sl::StackPush<NativeUInt>(stack, GDT_KERNEL_DATA);
        sl::StackPush<NativeUInt>(stack, realStackStart);
        sl::StackPush<NativeUInt>(stack, 0x202); //interrupts enabled, everything else set to default
        if (userspace)
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
