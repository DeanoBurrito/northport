#pragma once

#include <core/RunLevels.h>
#include <Atomic.h>
#include <containers/Queue.h>
#include <Types.h>

namespace Npk::Core
{
    struct ShootdownDetails
    {
        DpcStore* onComplete;
        sl::Atomic<size_t> pending;
        uintptr_t base;
        size_t length;
    };

    using ShootdownQueue = sl::QueueMpSc<ShootdownDetails>;
    using TlbShootdown = ShootdownQueue::Item;
    using SmpMailCallback = void (*)(void* arg);

    void InitLocalSmpMailbox();
    void ProcessLocalMail();
    void SendSmpMail(size_t destCore, SmpMailCallback callback, void* arg);
    void SendShootdown(size_t destCore, TlbShootdown* shootdown);
    void PanicAllCores();
}
