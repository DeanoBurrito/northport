#include <tasking/Process.h>
#include <tasking/Scheduler.h>

namespace Npk::Tasking
{
    Process* Process::Create()
    {
        return Scheduler::Global().CreateProcess();
    }
}
