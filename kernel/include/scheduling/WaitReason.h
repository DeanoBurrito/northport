#pragma once

#include <stddef.h>
#include <scheduling/Thread.h>

namespace Kernel::Scheduling
{
    enum class WaitReasonType : size_t
    {
        Sleep
    };
    
    struct WaitReason
    {
        sl::Vector<Thread*> waitingThreads;
        WaitReasonType type;
    };
}
