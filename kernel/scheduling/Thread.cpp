#include <scheduling/Thread.h>
#include <scheduling/Scheduler.h>
#include <devices/SystemClock.h>
#include <Locks.h>

namespace Kernel::Scheduling
{
    Thread* Thread::Current()
    { return CoreLocal()->ptrs[CoreLocalIndices::CurrentThread].As<Thread>(); }

    void Thread::Start(sl::NativePtr arg)
    {
        InterruptLock intLock;
        sl::ScopedSpinlock scopeLock(&lock);

        auto maybeStack = parent->VMM()->GetPhysAddr(programStack.raw - 8);
        sl::NativePtr stackAccess = EnsureHigherHalfAddr(maybeStack->raw + 8);
        stackAccess.As<StoredRegisters>()->rdi = arg.raw;
        runState = ThreadState::Running;
    }

    void Thread::Exit()
    {
        bool localExit = false;
        {
            InterruptLock intLock;
            runState = ThreadState::PendingCleanup;
            localExit = (Thread::Current() == this);
            Scheduler::Global()->RemoveThread(id);
        }

        //we only want to reschedule if we are exiting the current thread
        if (localExit)
        {
            Scheduler::Global()->Yield();
            __builtin_unreachable();
        }
    }

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
