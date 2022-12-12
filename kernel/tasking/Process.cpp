#include <tasking/Process.h>
#include <tasking/Scheduler.h>
#include <debug/Log.h>

namespace Npk::Tasking
{
    Process* Process::Create()
    {
        return Scheduler::Global().CreateProcess();
    }

    void Process::Kill() const
    {
        Scheduler::Global().DestroyProcess(id);
    }
}
