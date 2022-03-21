#pragma once

#include <containers/Vector.h>
#include <scheduling/Thread.h>

#define SCHEDULER_CLEANUP_THREAD_FREQ 100

namespace Kernel::Scheduling
{
    struct CleanupData
    {
        char lock;
        sl::Vector<ThreadGroup*> processes;
    };

    void CleanupThreadMain(void* arg);
}
