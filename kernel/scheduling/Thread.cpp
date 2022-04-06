#include <scheduling/Thread.h>
#include <scheduling/Scheduler.h>
#include <Locks.h>

namespace Kernel::Scheduling
{
    Thread* Thread::Current()
    { return GetCoreLocal()->ptrs[CoreLocalIndices::CurrentThread].As<Thread>(); }

    void Thread::Start(sl::NativePtr arg)
    {
        InterruptLock intLock;
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
        //NOTE: after calling exit() this thread's instance is actually deleted, we cannot use any instance variables or use the id for anything.
        Scheduler::Global()->Yield();
    }

    void Thread::Kill()
    {}

    void Thread::Sleep(size_t millis)
    {}
}
