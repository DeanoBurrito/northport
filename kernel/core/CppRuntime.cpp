#include <Core.hpp>

extern "C"
{
    uintptr_t __stack_chk_guard = static_cast<uintptr_t>(0x6B6375662C6C6557);

    void __stack_chk_fail()
    {
        NPK_UNREACHABLE();
    }

    int __cxa_atexit()
    {
        return 0;
    }

    void* __dso_handle;

    void __cxa_pure_virtual()
    {
        NPK_UNREACHABLE();
    }
}
