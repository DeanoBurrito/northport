#pragma once

#include <stddef.h>
#include <containers/Queue.h>

namespace Npk::Core
{
    enum class RunLevel
    {
        Normal = 0,
        Apc = 1,
        Dpc = 2,
        Clock = 3,
        Interrupt = 4,
    };

    constexpr bool operator<(const RunLevel a, const RunLevel b)
    { return static_cast<unsigned>(a) < static_cast<unsigned>(b); }

    constexpr RunLevel operator--(RunLevel& x, int)
    { 
        const unsigned asInt = static_cast<unsigned>(x);
        x = static_cast<RunLevel>(asInt - 1);
        return static_cast<RunLevel>(asInt);
    }

    using DpcEntry = void (*)(void* arg);

    struct Dpc
    {
        DpcEntry function;
        void* arg;
    };

    using DpcQueue = sl::QueueMpSc<Dpc>;
    using DpcStore = DpcQueue::Item;

    struct Apc
    {
        DpcEntry function;
        void* arg;
        size_t threadId;
    };

    using ApcQueue = sl::QueueMpSc<Apc>;
    using ApcStore = ApcQueue::Item;

    const char* RunLevelName(RunLevel rl);
    RunLevel RaiseRunLevel(RunLevel newRl);
    void LowerRunLevel(RunLevel newRl);

    void QueueDpc(DpcStore* dpc);
    void QueueApc(ApcStore* apc);
    void QueueRemoteDpc(size_t coreId, DpcStore* dpc);
}

using Npk::Core::RunLevel;
