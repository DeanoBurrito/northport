#include <events/Dispatch.h>
#include <debug/Log.h>

namespace Npk::Events
{
    constexpr const char* EventTypeStrs[] = 
    {
        "error", "new pci device (by id)", "new pci device (by class)"
    };

    EventDispatcher dispatchers[(size_t)EventType::Count];

    size_t PublishEvent(EventType type, void* arg)
    {
        if ((size_t)type >= (size_t)EventType::Count || type == EventType::Error)
        {
            Log("Attempt to publish invalid event type %lu", LogLevel::Error, (size_t)type);
            return 0;
        }

        ASSERT(CoreLocal().runLevel == RunLevel::Normal, "Bad run level");
        EventDispatcher& dispatch = dispatchers[(size_t)type];
        dispatch.lock.ReaderLock();
        
        size_t publishedCount = 0; 
        const EventItem item { .type = type, .arg = arg };

        for (auto funnel = dispatch.funnels.Begin(); funnel != dispatch.funnels.End(); ++funnel)
        {
            if (funnel->filter != FunnelNoFilter)
            {
                if (funnel->filter >= dispatch.filters.Size())
                {
                    Log("Invalid funnel id: %lu does not exist for event %lu (%s).", LogLevel::Error,
                        funnel->filter, (size_t)type, EventTypeStrs[(size_t)type]);
                    continue;
                }

                auto& filter = dispatch.filters[funnel->filter];
                if (!filter(arg, funnel->filterData))
                    continue;
            }
        
            //funnel has no filter, or filter successfully matched: push the event!
            funnel->queue->Push(item);
            publishedCount++;
        }

        dispatch.lock.ReaderUnlock();
        return publishedCount;
    }

    EventDispatcher* GetDispatch(EventType type)
    {
        if (type == EventType::Error || (size_t)type >= (size_t)EventType::Count)
            return nullptr;

        return &dispatchers[(size_t)type];
    }
}
