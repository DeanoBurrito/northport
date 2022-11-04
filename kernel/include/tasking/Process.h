#pragma once

#include <memory/Vmm.h>
#include <containers/Vector.h>

namespace Npk::Tasking
{
    class Scheduler;
    class Thread;

    class Process
    {
    friend Scheduler;
    private:
        VMM vmm;
        sl::Vector<Thread*> threads;
        size_t id;

    public:
        static Process* Create(void* environment);

        [[gnu::always_inline]]
        inline size_t Id()
        { return id; }
    };
}
