#pragma once

#include <stdint.h>

namespace Npk::Tasking
{
    struct DeferredCall
    {
        void (*function)(void *arg);
        void* arg;
    };

    //these are convinience functions for code that interacts with DPCs but doesn't need
    //the full scheduler and all it's many includes.
    void DpcQueue(void (*function)(void* arg), void* arg);
    void DpcExit();
}
