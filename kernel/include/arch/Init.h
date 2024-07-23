#pragma once

#include <arch/__Select.h>
#include <stddef.h>

namespace Npk
{
    //called once on the bsp, immediately after the kernel is in control of the machine.
    void ArchKernelEntry();

    //called once on the bsp, after memory management is available.
    void ArchLateKernelEntry();

    //called early in the init-sequence for each core, allows set up of core-local data structures.
    void ArchInitCore(size_t myId);

    //called from inside the init thread.
    void ArchThreadedInit();
}

#ifdef NPK_ARCH_INCLUDE_INIT
#include NPK_ARCH_INCLUDE_INIT
#endif
