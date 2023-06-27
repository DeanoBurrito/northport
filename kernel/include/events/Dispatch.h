#pragma once

#include <events/EventQueue.h>
#include <containers/Vector.h>
#include <containers/LinkedList.h>

namespace Npk::Events
{
    struct QueueFunnel
    {
        EventQueue* queue;
        size_t filter;
        void* filterData;
    };

    constexpr size_t FunnelNoFilter = -1ul;
    using FunnelCallbackFunc = bool (*)(void* arg, void* filter);

    struct EventDispatcher
    {
    private:
    public:
        sl::Vector<FunnelCallbackFunc> filters;
        sl::LinkedList<QueueFunnel> funnels;
        sl::RwLock lock;
    };

    size_t PublishEvent(EventType type, void* arg);
    EventDispatcher* GetDispatch(EventType type);
}

