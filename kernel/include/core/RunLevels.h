#pragma once

#include <Types.h>
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
}

using Npk::Core::RunLevel;
