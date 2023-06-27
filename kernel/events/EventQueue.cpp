#include <events/EventQueue.h>
#include <events/Dispatch.h>
#include <debug/Log.h>

namespace Npk::Events
{
    bool EventQueue::Subscribe(EventType type, bool retroactive, size_t filterType, void* filterData)
    {
        if ((size_t)type >= (size_t)EventType::Count || type == EventType::Error)
        {
            Log("Attempt to subscribe to invalid event type %lu", LogLevel::Error, (size_t)type);
            return false;
        }

        ASSERT(CoreLocal().runLevel == RunLevel::Normal, "EventQueue operations must occur at normal run level");
        if (filterType != FunnelNoFilter)
            ASSERT(filterData != nullptr, "Bad funnel data");

        ASSERT(!retroactive, "TODO: not implemented yet");

        EventDispatcher* dispatch = GetDispatch(type);
        if (dispatch == nullptr)
        {
            Log("Attempt to subscribe to invalid event type %lu", LogLevel::Error, (size_t)type);
            return false;
        }
        
        dispatch->lock.WriterLock();
        QueueFunnel& funnel = dispatch->funnels.EmplaceBack();
        funnel.queue = this;
        funnel.filter = filterType;
        funnel.filterData = filterData;
        dispatch->lock.WriterUnlock();

        return true;
    }

    bool EventQueue::Unsubscribe(EventType type)
    {
        ASSERT_UNREACHABLE();
    }

    void EventQueue::Push(EventItem item)
    {
        using Item = sl::QueueMpSc<EventItem>::Item;
        Item* qi = new Item();
        qi->data = sl::Move(item);

        queue.Push(qi);
        waitable.Trigger();
    }

    EventItem EventQueue::Pop(sl::Opt<size_t> timeout)
    {
        ASSERT(!timeout.HasValue(), "TODO: local timers + composite wait events");
        EventItem item;
        while (!TryPop(item))
            waitable.Wait();

        return item;
    }

    bool EventQueue::TryPop(EventItem& out)
    {
        auto* head = queue.Pop();
        return head != nullptr;
    }
}
