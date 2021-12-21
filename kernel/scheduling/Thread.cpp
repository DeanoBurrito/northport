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
    {}

    void Thread::Exit()
    {}

    size_t Thread::GetId() const
    { return threadId; }
}
