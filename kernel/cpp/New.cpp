#include <stddef.h>
#include <memory/Heap.h>

using Npk::Memory::Heap;

extern "C"
{
    void* malloc(size_t size)
    {
        return Heap::Global().Alloc(size);
    }

    void free(void* ptr)
    {
        Heap::Global().Free(ptr);
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

void operator delete(void* ptr) noexcept
{
    Heap::Global().Free(ptr);
}

void operator delete(void* ptr, unsigned long) noexcept
{
    Heap::Global().Free(ptr);
}

void operator delete[](void* ptr) noexcept
{
    Heap::Global().Free(ptr);
}

void operator delete[](void* ptr, unsigned long) noexcept
{
    Heap::Global().Free(ptr);
}
