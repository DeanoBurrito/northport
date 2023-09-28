#include <drivers/api/Api.h>
#include <arch/Platform.h>
#include <debug/Log.h>
#include <debug/NanoPrintf.h>
#include <memory/Heap.h>
#include <tasking/Thread.h>

/* This file serves as the bridge between internal kernel functions and the
 * driver API. Which is why this file has a bit of everything and doesn't do
 * anything particularly interesting: it's mostly marshalling data around.
 */

extern "C"
{
    [[gnu::used]]
    void* npk_heap_alloc(size_t count)
    {
        return Npk::Memory::Heap::Global().Alloc(count);
    }

    [[gnu::used]]
    void npk_heap_free(void* ptr, size_t count)
    {
        Npk::Memory::Heap::Global().Free(ptr, count);
    }

    [[gnu::used]]
    void npk_log(const char* str, npk_log_level level)
    {
        Log("[DRIVER] %s", static_cast<LogLevel>(level), str);
    }

    [[gnu::used]]
    void npk_panic(const char* why)
    {
        Npk::Panic(why);
    }
    
    [[gnu::used]]
    void npk_thread_exit(size_t code)
    {
        Npk::Tasking::Thread::Current().Exit(code);
    }
}

