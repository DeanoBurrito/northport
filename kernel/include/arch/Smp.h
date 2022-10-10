#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Npk
{
    void InitSmp();
    void BootAllProcessors(uintptr_t entry);
}
