#pragma once

#include <stdint.h>
#include <stddef.h>

namespace Npk::Boot
{
#ifdef NP_INCLUDE_LIMINE_BOOTSTRAP
    void PerformLimineBootstrap(uintptr_t physLoadBase, uintptr_t virtLoadBase, size_t bspId, uintptr_t dtb);
#endif
}
