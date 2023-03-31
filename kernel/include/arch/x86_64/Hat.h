#pragma once

#include <stdint.h>

namespace Npk
{
    enum class HatFlags : uint64_t
    {
        None = 0,
        Write = 1 << 1,
        User = 1 << 2,
        Global = 1 << 8,
        Execute = 1ul << 63,
    };
}
