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

        sl::NativePtr(programStack).As<StoredRegisters>()->rsi = arg.raw;
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

    const sl::Vector<Thread*>& ThreadGroup::GetThreads() const
    { return threads; }

    const Thread* ThreadGroup::GetParent() const
    { return parent; }

    size_t ThreadGroup::GetId() const
    { return id; }
}
