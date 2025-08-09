#pragma once

#include <Types.h>

namespace Npk
{
    constexpr uint8_t MinPriority = 0;
    constexpr uint8_t MaxPriority = 255;
    constexpr uint8_t IdlePriority = 0;
    constexpr uint8_t MinRtPriority = 0;
    constexpr uint8_t MaxRtPriority = MaxPriority / 2;
    constexpr uint8_t MinTsPriority = IdlePriority + 1;
    constexpr uint8_t MaxTsPriority = MaxRtPriority - 1;
}
