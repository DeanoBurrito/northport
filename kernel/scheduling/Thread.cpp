#include <scheduling/Thread.h>
#include <scheduling/Scheduler.h>
#include <Locks.h>

namespace Kernel::Scheduling
{
    Thread* Thread::Current()
    { return GetCoreLocal()->ptrs[CoreLocalIndices::CurrentThread].As<Thread>(); }

    size_t Thread::GetId() const
    { return id; }

    ThreadFlags Thread::GetFlags() const
    { return flags; }

    ThreadState Thread::GetState() const
    { return runState; }
    
    ThreadGroup* Thread::GetParent() const
    { return parent; }

    void Thread::Start(sl::NativePtr arg)
    {
        sl::ScopedSpinlock scopeLock(&lock);

        //we'll need the physical address of the program's stack, and then access it through the hhdm to avoid swapping CR3.
        auto maybeStack = parent->VMM()->PageTables().GetPhysicalAddress(programStack);
        sl::NativePtr stackPhys = *maybeStack;
        stackPhys = EnsureHigherHalfAddr(maybeStack->ptr);
        stackPhys.As<StoredRegisters>()->rsi = arg.raw;
        runState = ThreadState::Running;
    }

    void Thread::Exit()
    {
        runState = ThreadState::PendingCleanup;
        Scheduler::Global()->RemoveThread(this->id);
        Scheduler::Global()->Yield();
    }

    void Thread::Kill()
    {}

    void Thread::Sleep(size_t millis)
    {}

    const sl::Vector<Thread*>& ThreadGroup::Threads() const
    { return threads; }

    const Thread* ThreadGroup::ParentThread() const
    { return parent; }

    size_t ThreadGroup::Id() const
    { return id; }

    Memory::VirtualMemoryManager* ThreadGroup::VMM()
    { return &vmm; }
}
