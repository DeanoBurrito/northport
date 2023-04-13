#include <tasking/ServiceThreads.h>
#include <tasking/Scheduler.h>

namespace Npk::Tasking
{
    void SchedulerCleanupThreadMain(void* cleanupData)
    {
        CleanupData* data = static_cast<CleanupData*>(cleanupData);

        while (true)
        {
            data->updated.Wait();
            Log("Threads pending cleanup: %lu", LogLevel::Debug, data->threads.Size());
            //TODO: actually destroy threads here
            //it would be a shame to lose this O(0) efficiency though
        }

        ASSERT_UNREACHABLE();
    }
}
