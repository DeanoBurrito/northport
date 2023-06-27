#pragma once

#include <tasking/Waitable.h>
#include <containers/Queue.h>
#include <Optional.h>

namespace Npk::Events
{
    enum class EventType : uint64_t
    {
        Error = 0,
        NewPciId = 1,
        NewPciClass = 2,

        Count,
    };

    struct EventItem
    {
        EventType type;
        void* arg;
    };

    class EventQueue
    {
    private:
        sl::QueueMpSc<EventItem> queue;
        Tasking::Waitable waitable;

    public:
        EventQueue() = default;
        
        ~EventQueue() = delete;
        EventQueue(const EventQueue&) = delete;
        EventQueue& operator=(const EventQueue&) = delete;
        EventQueue(EventQueue&&) = delete;
        EventQueue& operator=(EventQueue&&) = delete;

        bool Subscribe(EventType type, bool retroactive, size_t filterType, void* filterData);
        bool Unsubscribe(EventType type);

        void Push(EventItem item);
        EventItem Pop(sl::Opt<size_t> timeout = {});
        bool TryPop(EventItem& out);
    };
}

