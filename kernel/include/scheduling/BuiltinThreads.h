#pragma once

#include <containers/Vector.h>
#include <scheduling/Thread.h>

namespace Kernel::Scheduling
{
    struct CleanupData
    {
        char lock;
        sl::Vector<ThreadGroup*> processes;
    };

    void CleanupThreadMain(void* arg);
}
