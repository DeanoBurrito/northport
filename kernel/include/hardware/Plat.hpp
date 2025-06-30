#pragma once

#include <Time.h>
#include <Compiler.h>

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

    SL_ALWAYS_INLINE
    void PlatStallFor(sl::TimeCount duration)
    {
        auto start = PlatReadTimestamp();
        auto end = duration.Rebase(start.Frequency).ticks + start.epoch;
        
        while (PlatReadTimestamp().epoch < end)
            asm volatile("");
    }

    void PlatSendIpi(void* id);
}
