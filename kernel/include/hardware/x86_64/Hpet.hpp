#pragma once

#include <Types.hpp>

namespace Npk
{
    bool InitHpet(uintptr_t& virtBase);

    bool HpetAvailable();
    bool HpetIs64Bit();
    uint64_t HpetRead();
    uint64_t HpetFrequency();
}
