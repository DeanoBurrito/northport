#pragma once

#include <stddef.h>
#include <Maths.h>

namespace np::Userland
{
    constexpr size_t HeapBaseAddr = 1 * GB;
    
    //called to setup a friendly environment for a user program to run in: cmd line args, and all that.
    void InitUserlandApp();
    //called to exit an application, cleans up resources and then exits with an error code (or 0 for success)
    void ExitUserlandApp(unsigned long exitCode = 0);
}
