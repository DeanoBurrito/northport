#pragma once

#include <stdint.h>
#include <Locks.h>
#include <Atomic.h>

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
        sl::TicketLock lock;

        Thread* next;
        sl::Atomic<size_t> activeCore;

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
