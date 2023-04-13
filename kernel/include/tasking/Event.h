#pragma once

#include <Locks.h>
#include <containers/Vector.h>

namespace Npk::Tasking
{
    class Event
    {
    private:
        sl::SpinLock lock;
        sl::Vector<size_t> waitingThreads;
        size_t pendingTriggers;

    public:
        constexpr Event() : waitingThreads(), pendingTriggers(0)
        {}

        void Wait();
        bool WouldWait();

        void Trigger(bool accumulate = false, size_t count = 1);
    };

    //TODO: size_t WaitEvents(Event[] events);
    //bool WaitFor(Event event, size_t nanos);
}
