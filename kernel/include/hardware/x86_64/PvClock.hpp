#pragma once

#include <lib/Types.hpp>
#include <lib/Compiler.hpp>
#include <lib/Optional.hpp>

namespace Npk
{
    struct SL_PACKED(PvSystemTime
    {
        uint32_t version;
        uint32_t reserved0;
        uint64_t tscReference;
        uint64_t systemTime;
        uint32_t tscToSystemMul;
        int8_t tscShift;
        uint8_t flags;
        uint8_t reserved[2];
    });

    bool TryInitPvClocks(uintptr_t& virtBase);
    bool LocalPvClockInit();

    bool PvClockAvailable();
    uint32_t PvClockVersion();
    PvSystemTime ReadPvClock();
    uint64_t PvClockFrequency(const PvSystemTime& clock);
    sl::Opt<uint64_t> PvClockTscFrequency();
}
