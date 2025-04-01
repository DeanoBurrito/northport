#include <Kernel.hpp>

extern "C"
{
    uintptr_t __stack_chk_guard = static_cast<uintptr_t>(0x57656C2C6675636B);

    void __stack_chk_fail()
    {
        NPK_UNREACHABLE();
    }

    int __cxa_atexit()
    {
        NPK_UNREACHABLE();
    }

    void* __dso_handle;

    void __cxa_pure_virtual()
    {
        NPK_UNREACHABLE();
    }
}
