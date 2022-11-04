#include <tasking/Thread.h>
#include <tasking/Scheduler.h>

namespace Npk::Tasking
{
    Thread* Thread::Create(void (*entry)(void*), void* arg, Process* parent)
    {
        return Scheduler::Global().CreateThread(entry, arg, parent);
    }
}
