#pragma once

#include <stdint.h>

namespace Npk
{
    enum class HatFlags : uint64_t
    {
        None = 0,
        Write = 1 << 2,
        Execute = 1 << 3,
        User = 1 << 4,
        Global = 1 << 5
    };
}
