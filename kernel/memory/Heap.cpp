#include <memory/Heap.h>

namespace Npk::Memory
{
    Heap globalHeap;
    Heap& Heap::Global()
    { return globalHeap; }

    void* Heap::Alloc(size_t size)
    {}

    void Heap::Free(void* ptr)
    {}
}
