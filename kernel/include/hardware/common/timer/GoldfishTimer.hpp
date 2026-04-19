#pragma once

#include <lib/Time.hpp>
#include <lib/Compiler.hpp>

namespace Npk
{
    bool InitGoldfishTimer(uintptr_t& virtBase, Paddr foundAt);

    bool GoldfishTimerAvailable();
    uint64_t GoldfishTimerRead();
    void GoldfishTimerArm(uint64_t timestamp);

    SL_ALWAYS_INLINE
    uint64_t GoldfishTimerFrequency()
    {
        return sl::Nanos;
    }
}
