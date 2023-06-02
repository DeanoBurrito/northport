#include <stddef.h>
#include <memory/Heap.h>

using Npk::Memory::Heap;

extern "C"
{
    void* malloc(size_t size)
    {
        return Heap::Global().Alloc(size);
    }

    void free(void* ptr, size_t length)
    {
        Heap::Global().Free(ptr, length);
    }
}

void* operator new(size_t size)
{
    return Heap::Global().Alloc(size);
}

void* operator new[](size_t size)
{
    return Heap::Global().Alloc(size);
}

//This operator is required as part of the C++ spec, so it's defined.
//however we only allow sized deallocations in the kernel, so calling it
//is an error and will panic (also see `operator delete[](void*)` below).
//Not having these operators will emit warnings (or errors even) on some compilers.
void operator delete(void* ptr) noexcept
{
    (void)ptr;
    ASSERT_UNREACHABLE()
}

void operator delete(void* ptr, size_t length) noexcept
{
    Heap::Global().Free(ptr, length);
}

void operator delete[](void* ptr) noexcept
{
    (void)ptr;
    ASSERT_UNREACHABLE()
}

void operator delete[](void* ptr, size_t length) noexcept
{
    Heap::Global().Free(ptr, length);
}
