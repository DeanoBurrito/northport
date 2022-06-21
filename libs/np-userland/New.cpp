#include <stddef.h>
#include <heap/UserHeap.h>
#include <SyscallFunctions.h>

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
        Log("__cxa_pure_virtual called. This should not happen.", np::Syscall::LogLevel::Error);
    }
    
    void __cxa_atexit()
    {
        Log("__cxa_atexit called.", np::Syscall::LogLevel::Debug);
    }

    void __dso_handle()
    {
        Log("__dso_handle called.", np::Syscall::LogLevel::Debug);
    }
}
