#include <Exit.h>
#include <core/Log.h>

namespace Npk
{
    void KernelExit(bool poweroff)
    {
        (void)poweroff;
        ASSERT_UNREACHABLE();
    }

    bool KernelLoadSuccessor(void* next)
    {
        (void)next;
        //TODO: check for any successor handlers (example one is elf64 + LBP)
        ASSERT_UNREACHABLE();
    }
}
