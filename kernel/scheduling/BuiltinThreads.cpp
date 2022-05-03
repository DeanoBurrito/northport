#include <scheduling/BuiltinThreads.h>
#include <scheduling/Scheduler.h>
#include <devices/DeviceManager.h>
#include <Locks.h>

namespace Kernel::Scheduling
{
    constexpr size_t SchedulerCleanupThreadFreq = 10;
    
    void CleanupThreadMain(void* arg)
    {
        CleanupData* data = reinterpret_cast<CleanupData*>(arg);

        while (true)
        {
            sl::SpinlockAcquire(&data->lock);
            for (size_t i = 0; i < data->processes.Size(); i++)
            {
                data->processes[i]->VMM()->Deinit();
                delete data->processes[i];
            }
            data->processes.Clear();

            sl::SpinlockRelease(&data->lock);
            Thread::Current()->Sleep(1000 / SchedulerCleanupThreadFreq);
        }
    }
}
