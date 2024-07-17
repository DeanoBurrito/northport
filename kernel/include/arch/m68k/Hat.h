#pragma once

#include <stdint.h>

namespace Npk
{
    enum class HatFlags : uint32_t
    {
        None = 0,
        Write,
        User,
        Global,
        Execute
    };
}
