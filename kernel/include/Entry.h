#pragma once

#include <stddef.h>

namespace Npk
{
    //started on the first core to finish its local init and begin scheduling.
    void InitThread(void*);

    //called by boot protocol AP entry code.
    void PerCoreEntry(size_t myId);
    //called by boot protocol AP entry code after PerCoreEntry(), and by BSP later on.
    [[noreturn]]
    void ExitCoreInit();
}
