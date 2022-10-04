#include <stdint.h>
#include <debug/Log.h>

extern "C"
{
    uintptr_t __stack_chk_guard = 0x6F616D6C6F616D6C;

    void __stack_chk_fail()
    {
        ASSERT_UNREACHABLE();
    }

    void __cxa_atexit()
    {
        ASSERT_UNREACHABLE();
    }

    void __dso_handle()
    {
        ASSERT_UNREACHABLE();
    }
}
