#include <CoreApi.hpp>
#include <Memory.h>

namespace Npk
{
    static PageInfo* TakePage(SystemDomain& dom)
    {
        if (!dom.freeLists.zeroed.Empty())
            return dom.freeLists.zeroed.PopFront();
        if (!dom.freeLists.free.Empty())
        {
            PageInfo* page = dom.freeLists.free.PopFront();
            auto access = AccessPage(page);
            sl::MemSet(access->value, 0, PageSize());

            return page;
        }

        return nullptr;
    }

    PageInfo* AllocPage(bool canFail)
    {
        SystemDomain& dom = MySystemDomain();

        dom.freeLists.lock.Lock();
        PageInfo* page = TakePage(dom);
        dom.freeLists.lock.Unlock();

        if (page != nullptr || canFail)
            return page;

        NPK_UNREACHABLE(); //TODO: wait for a page to be available
    }

    void FreePage(PageInfo* page)
    {
        SystemDomain& dom = MySystemDomain();

        dom.freeLists.lock.Lock();
        dom.freeLists.free.PushBack(page);
        dom.freeLists.lock.Unlock();
    }
}
