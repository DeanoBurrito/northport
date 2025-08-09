#pragma once

#include <CoreTypes.hpp>

namespace Npk
{
    bool TryInitPvClocks(uintptr_t& virtBase);
    uint64_t ReadPvSystemTime();
}
