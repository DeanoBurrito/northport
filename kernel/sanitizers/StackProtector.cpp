#include <stdint.h>
#include <Log.h>

extern "C"
{
    uintptr_t __stack_chk_guard = 0xc7b1dd30df4c8b88;

    void __stack_chk_fail()
    {
        Log("Stack smashing detected!", Kernel::LogSeverity::Fatal);
    }
}
