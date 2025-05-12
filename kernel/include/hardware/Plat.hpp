#pragma once

#include <Time.h>

namespace Npk
{
    struct InitState;

    void PlatInitEarly();
    size_t PlatGetCpuCount(InitState& state);
    void PlatInitDomain0(InitState& state);
    void PlatInitFull(uintptr_t& virtBase);
    void PlatBootAps(uintptr_t stacks, uintptr_t perCpuStores, size_t perCpuStride);

    void PlatSetAlarm(sl::TimePoint expiry);
    sl::TimePoint PlatReadTimestamp();
}
