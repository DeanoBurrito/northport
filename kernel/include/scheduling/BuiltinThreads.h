#pragma once

#include <containers/Vector.h>
#include <scheduling/Thread.h>

namespace Kernel::Scheduling
{
    struct CleanupData
    {
        char lock;
        sl::Vector<ThreadGroup*> groups;
        sl::Vector<Thread*> threads;
    };

    void CleanupThreadMain(void* arg);
    void DeviceEventPump(void* arg);
}
