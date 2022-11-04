#include <tasking/Process.h>
#include <tasking/Scheduler.h>

namespace Npk::Tasking
{
    Process* Process::Create(void* environment)
    {
        return Scheduler::Global().CreateProcess(environment);
    }
}
