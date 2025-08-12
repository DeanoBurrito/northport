#pragma once

#include <Types.hpp>

namespace Npk
{
    bool TryInitPvClocks(uintptr_t& virtBase);
    uint64_t ReadPvSystemTime();
}
