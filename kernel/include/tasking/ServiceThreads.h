#pragma once

#include <tasking/Thread.h>
#include <tasking/Waitable.h>
#include <containers/LinkedList.h>
#include <Locks.h>

namespace Npk::Tasking
{
    struct CleanupData
    {
        sl::LinkedList<Thread*> threads;
        sl::LinkedList<Process*> processes;
        sl::TicketLock lock;
        Waitable updated;
    };

    void SchedulerCleanupThreadMain(void* cleanupData);
}
