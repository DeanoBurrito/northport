#pragma once

#include <stdint.h>
#include <arch/Platform.h>

namespace Npk::Tasking
{
    class Scheduler;
    class Process;

    enum class ThreadState
    {
        Setup = 0,
        Runnable,
        Dead,
        Sleeping
    };

    class Thread
    {
    friend Scheduler;
    private:
        TrapFrame* frame;
        struct
        {
            uintptr_t base;
            size_t length;
        } stack;

        ThreadState state = ThreadState::Setup;
        Process* parent;
        size_t id;

    public:
        static Thread* Create(void (*entry)(void*), void* arg, Process* parent = nullptr);

        [[gnu::always_inline]]
        inline size_t Id()
        { return id; }
    };
}
