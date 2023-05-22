#include <tasking/Thread.h>
#include <tasking/Scheduler.h>

namespace Npk::Tasking
{
    Thread* Thread::Create(void (*entry)(void*), void* arg, Process* parent)
    {
        return Scheduler::Global().CreateThread(entry, arg, parent);
    }

    Thread& Thread::Current()
    { return *reinterpret_cast<Thread*>(CoreLocal()[LocalPtr::Thread]); }

    void Thread::Start()
    {
        Scheduler::Global().EnqueueThread(id);
    }

    void Thread::Exit(size_t code)
    {
        Scheduler::Global().DestroyThread(id, code);
    }
}
