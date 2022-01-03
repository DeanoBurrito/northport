#include <scheduling/Thread.h>
#include <scheduling/Scheduler.h>

namespace Kernel::Scheduling
{
    void Thread::Cleanup()
    {
        //TODO: free memory used by stack
    }

    Thread* Thread::Current()
    {
        return Scheduler::Local()->GetCurrentThread();
    }

    void Thread::Start(sl::NativePtr arg)
    {
        regs->rsi = arg.raw; //depends on abi (we're using sys v x86_64)
        runState = ThreadState::Running;
    }

    void Thread::Exit()
    {}

    size_t Thread::GetId() const
    { return threadId; }

    ThreadState Thread::GetState() const
    { return runState; }

    ThreadFlags Thread::GetFlags() const
    { return flags; }
}
