#include <Log.h>

extern "C"
{
    uintptr_t __stack_chk_guard = 0x57656C2C6675636B;

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

    void __cxa_pure_virtual()
    {
        ASSERT_UNREACHABLE()
    }
}
