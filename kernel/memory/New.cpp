#include <stddef.h>
#include <memory/KernelHeap.h>

void* operator new(size_t size)
{
    return malloc(size);
}

void* operator new[](size_t size)
{
    return malloc(size);
}

void operator delete(void* ptr) noexcept
{
    free(ptr);
}

void operator delete(void* ptr, unsigned long) noexcept
{
    free(ptr);
}

void operator delete[](void* ptr) noexcept
{
    free(ptr);
}

void operator delete[](void* ptr, unsigned long) noexcept
{
    free(ptr);
}
