#include <CorePrivate.hpp>

namespace Npk
{
    void QueueWorkItem(WorkItem* item, sl::Opt<CpuId> who)
    {
        NPK_CHECK(item != nullptr, );

        CpuId target = MyCoreId();
        if (who.HasValue())
            target = *who;

        auto status = RemoteStatus(target);
        NPK_CHECK(status != nullptr, );

        sl::ScopedLock scopeLock(status->workItemsLock);
        status->workItems.PushBack(item);
    }
}

namespace Npk::Private
{
    void WorkThreadEntry(void* arg)
    {
        (void)arg;

        while (true)
        {
            auto status = RemoteStatus(MyCoreId());
            NPK_ASSERT(status != nullptr);

            WorkItemQueue items {};

            //TODO: think about the effects of this lock, when can work be posted
            //here (higher IPLs would be nice), meaning we need mutex with them.
            status->workItemsLock.Lock();
            status->workItems.Exchange(items);
            status->workItemsLock.Unlock();

            while (!items.Empty())
            {
                auto item = items.PopFront();
                item->function(item, item->arg);
            }

            Yield();
        }
    }
}
