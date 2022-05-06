#include <scheduling/Thread.h>
#include <scheduling/Scheduler.h>
#include <devices/SystemClock.h>
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
        //NOTE: after calling exit() this thread's instance is actually deleted, we cannot use any instance variables or use the id for anything.
        bool localExit = false;
        {
            InterruptLock intLock;
            runState = ThreadState::PendingCleanup;
            localExit = (Thread::Current() == this);
            Scheduler::Global()->RemoveThread(this->id);
        }

        //we only want to reschedule if we are exiting the current thread
        if (localExit)
            Scheduler::Global()->Yield();
    }

    void Thread::Kill()
    {}

    void Thread::Sleep(size_t millis)
    {
        {
            InterruptLock intLock;
            sl::ScopedSpinlock scopeLock(&lock);
            runState = ThreadState::Sleeping;
            if (millis > 0)
                wakeTime = Devices::GetUptime() + millis;
            else
                wakeTime = 0;
        }

        Scheduling::Scheduler::Global()->Yield();
    }

    void Thread::SleepUntilEvent(size_t timeout)
    {
        {
            InterruptLock intLock;
            sl::ScopedSpinlock scopeLock(&lock);
            runState = ThreadState::SleepingForEvents;
            if (timeout > 0)
                wakeTime = Devices::GetUptime() + timeout;
            else
                wakeTime = 0;
        }

        Scheduling::Scheduler::Global()->Yield();
    }
}
