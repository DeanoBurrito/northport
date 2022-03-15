#include <Userland.h>
#include <GeneralHeap.h>

namespace np::Userland
{
    void InitUserlandApp()
    {
        //initialize the global heap so we have malloc()/free()
        GeneralHeap::Default().Init(heapStartingAddr, heapStartingSize);
    }
}
