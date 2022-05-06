#include <Userland.h>
#include <GeneralHeap.h>
#include <SyscallFunctions.h>

namespace np::Userland
{
    void InitUserlandApp()
    {
        //initialize the global heap so we have malloc()/free()
        GeneralHeap::Default().Init(heapStartingAddr, heapStartingSize);

        //map some memory really high above the heap as a work around for how
        //VMM::AllocateRange() works (a bump allocator based on the highest address so far)
        np::Syscall::MapMemory(heapGuardAddr, 0x1000, np::Syscall::MemoryMapFlags::None);
    }

    void ExitUserlandApp(unsigned long exitCode)
    {
        np::Syscall::Exit(exitCode);
    }
}
