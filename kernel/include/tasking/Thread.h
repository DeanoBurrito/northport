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
        Ready,
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
        size_t coreAffinity;

        Thread* next;

    public:
        static Thread* Create(void (*entry)(void*), void* arg, Process* parent = nullptr);
        static Thread& Current();

        [[gnu::always_inline]]
        inline size_t Id()
        { return id; }

        void Start();
        void Exit(size_t code);
    };
}
