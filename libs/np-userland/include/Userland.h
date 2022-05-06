#pragma once

#include <stddef.h>
#include <Maths.h>

namespace np::Userland
{
    constexpr size_t heapStartingSize = 16 * KB;
    constexpr size_t heapStartingAddr = 1 * GB;
    constexpr size_t heapMinAllocSize = 0x20; //TODO: increase this, and smaller allocs use slabs instead
    constexpr size_t heapExpandRequestSize = 4 * KB;
    constexpr size_t heapExpandFactor = 2;
    constexpr size_t heapGuardAddr = 16 * GB; //TODO: fix AllocateRange() to use a separate memory range so this isnt needed
    
    //called to setup a friendly environment for a user program to run in: cmd line args, and all that.
    void InitUserlandApp();
    //called to exit an application, cleans up resources and then exits with an error code (or 0 for success)
    void ExitUserlandApp(unsigned long exitCode = 0);
}
