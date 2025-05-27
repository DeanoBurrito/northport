#pragma once

#include <KernelTypes.hpp>

namespace Npk
{
    bool TryInitPvClocks(uintptr_t& virtBase);
    uint64_t ReadPvSystemTime();
}
