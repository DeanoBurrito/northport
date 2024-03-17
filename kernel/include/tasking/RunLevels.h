#pragma once

#include <stdint.h>
#include <containers/Queue.h>
#include <Span.h>
#include <Optional.h>

namespace Npk::Tasking
{
    enum class RunLevel : uint8_t
    {
        Normal = 0,
        Apc = 1,
        Dpc = 2,
        Clock = 3,
        Interrupt = 4,
    };

    constexpr bool operator<(const RunLevel a, const RunLevel b)
    { return static_cast<uint8_t>(a) < static_cast<uint8_t>(b); }

    constexpr RunLevel operator--(RunLevel& x, int)
    { 
        const uint8_t asInt = static_cast<uint8_t>(x);
        x = static_cast<RunLevel>(asInt - 1);
        return static_cast<RunLevel>(asInt);
    }

    using DpcEntry = void (*)(void* arg);

    struct Dpc
    {
        DpcEntry function;
        void* arg;
    };

    using DpcStore = sl::QueueMpSc<Dpc>::Item;

    struct Apc
    {
        DpcEntry function;
        void* arg;
        size_t threadId;
    };

    using ApcStore = sl::QueueMpSc<Apc>::Item;

    const char* GetRunLevelName(RunLevel runLevel);
    RunLevel RaiseRunLevel(RunLevel newLevel);
    sl::Opt<RunLevel> EnsureRunLevel(RunLevel level);
    void LowerRunLevel(RunLevel newLevel);

    void QueueDpc(DpcStore* dpc);
    void QueueApc(ApcStore* apc);
}

using Npk::Tasking::RunLevel;
