#include <stddef.h>
#include <memory/KernelHeap.h>
#include <Log.h>

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

extern "C"
{
    void __cxa_pure_virtual()
    {
        Log("__cxa_pure_virtual called. This should not happen.", Kernel::LogSeverity::Error);
    }
}
