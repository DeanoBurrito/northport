#include <core/Log.h>

extern "C"
{
    uintptr_t __stack_chk_guard = static_cast<uintptr_t>(0x57656C2C6675636B);

    void __stack_chk_fail()
    {
        ASSERT_UNREACHABLE();
    }

    int __cxa_atexit()
    {
        return 0;
    }

    void* __dso_handle;

    void __cxa_pure_virtual()
    {
        ASSERT_UNREACHABLE()
    }
}
