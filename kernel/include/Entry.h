#pragma once

#include <Types.h>

namespace Npk
{
    //called by boot protocol AP entry code.
    void PerCoreEntry(size_t myId);

    //called by boot protocol AP entry code after PerCoreEntry(), and by BSP later on.
    [[noreturn]]
    void ExitCoreInit();
}
