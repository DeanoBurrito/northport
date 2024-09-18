#include <core/Heap.h>
#include <core/Log.h>

namespace Npk::Core
{
    void InitHeap()
    { ASSERT_UNREACHABLE(); }

    void InitLocalHeapCache()
    { ASSERT_UNREACHABLE(); }

    void* WiredAlloc(size_t size)
    { ASSERT_UNREACHABLE(); }

    void WiredFree(void* ptr, size_t size)
    { ASSERT_UNREACHABLE(); }
}

void* operator new(size_t size)
{ ASSERT_UNREACHABLE(); }

void* operator new[](size_t size)
{ ASSERT_UNREACHABLE(); }
