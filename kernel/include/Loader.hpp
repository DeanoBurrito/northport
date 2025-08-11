#pragma once

#include <Types.hpp>
#include <Span.hpp>
#include <Optional.hpp>
#include <Time.hpp>

namespace Npk::Loader
{
    struct LoadState
    {
        uintptr_t directMapBase;
        Paddr kernelBase;
        CpuId bspId;
        sl::Opt<Paddr> rsdp;
        sl::Opt<Paddr> fdt;
        sl::Opt<sl::TimePoint> timeOffset;
        sl::StringSpan commandLine;
    };

    struct MemoryRange
    {
        Paddr base;
        size_t length;
    };

    LoadState GetEntryState();
    size_t GetUsableRanges(sl::Span<MemoryRange> ranges, size_t offset);
}
