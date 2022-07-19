#include <scheduling/BuiltinThreads.h>
#include <devices/DeviceManager.h>
#include <Locks.h>

namespace Kernel::Scheduling
{
    constexpr size_t SchedulerCleanupThreadFreq = 10;
    constexpr size_t DeviceEventPumpFreq = 100;
    
    void CleanupThreadMain(void* arg)
    {
        CleanupData* data = reinterpret_cast<CleanupData*>(arg);

        while (true)
        {
            sl::SpinlockAcquire(&data->lock);
            // for (size_t i = 0; i < data->threads.Size(); i++)
            // {
            //     if (!data->threads[i]->Parent()->CleanupChild(data->threads[i]))
            //         continue;
                
            //     delete data->threads[i];
            //     data->threads.Erase(i);
            // } TODO: implement cleaning up threads

            for (size_t i = 0; i < data->groups.Size(); i++)
            {
                data->groups[i]->VMM()->Deinit();
                delete data->groups[i];
            }
            data->groups.Clear();

            sl::SpinlockRelease(&data->lock);
            Thread::Current()->Sleep(1000 / SchedulerCleanupThreadFreq);
        }
    }

    void DeviceEventPump(void*)
    {
        while (true)
        {
            Devices::DeviceManager::Global()->EventPump();

            Thread::Current()->Sleep(1000 / DeviceEventPumpFreq);
        }
    }
}
