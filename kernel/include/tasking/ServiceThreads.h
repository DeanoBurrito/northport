#pragma once

#include <tasking/Thread.h>
#include <containers/LinkedList.h>
#include <Locks.h>

namespace Npk::Tasking
{
    struct CleanupData
    {
        sl::LinkedList<Thread*> threads;
        sl::TicketLock lock;
    };

    void SchedulerCleanupThreadMain(void* cleanupData);
}
