#pragma once

#include <core/Scheduler.h>

namespace Npk
{
    //creates a kernel thread complete with a stack, does *not* enqueue it.
    sl::Opt<Core::SchedulerObj*> CreateKernelThread(void (*entry)(void*), void* arg);

    //cleans up resources used by a kernel thread, thread must be dequeued and its
    //stack not in-use.
    void DestroyKernelThread(Core::SchedulerObj* thread);
}
