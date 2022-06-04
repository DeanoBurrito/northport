#include <Userland.h>
#include <GeneralHeap.h>
#include <SyscallFunctions.h>

namespace np::Userland
{
    void InitUserlandApp()
    {
        //initialize the global heap so we have malloc()/free()
        GeneralHeap::Default().Init(heapStartingAddr, heapStartingSize);
    }

    void ExitUserlandApp(unsigned long exitCode)
    {
        np::Syscall::Exit(exitCode);
    }
}
