#include <Userland.h>
#include <heap/UserHeap.h>
#include <SyscallFunctions.h>

namespace np::Userland
{
    void InitUserlandApp()
    {
        //initialize the global heap so we have malloc()/free()
        UserHeap::Global()->Init(HeapBaseAddr, false);
    }

    void ExitUserlandApp(unsigned long exitCode)
    {
        np::Syscall::Exit(exitCode);
    }
}
