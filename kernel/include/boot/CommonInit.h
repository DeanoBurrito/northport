#pragma once

#include <stddef.h>

namespace Npk
{
    bool CoresInEarlyInit(); //TODO: hacky workaround, better solution? (see early anon demand paging)
    void InitThread(void*);

    void PerCoreEntry(size_t myId); //called by boot protocol AP spinup code.
    extern "C" void KernelEntry(); //called by boot protocol entry function.
}
