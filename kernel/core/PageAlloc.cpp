#include <Core.hpp>
#include <Memory.hpp>

namespace Npk
{
    //NOTE: assumes dom.freeLists.lock is held
    static PageInfo* TakePage(SystemDomain& dom)
    {
        if (!dom.freeLists.zeroed.Empty())
        {
            dom.freeLists.pageCount--;
            return dom.freeLists.zeroed.PopFront();
        }

        if (!dom.freeLists.free.Empty())
        {
            PageInfo* page = dom.freeLists.free.PopFront();

            if (page->pm.count > 1)
            {
                PageInfo* next = page + 1;
                next->pm.count = page->pm.count - 1;
                dom.freeLists.free.PushBack(next);
            }

            auto access = AccessPage(page);
            sl::MemSet(access->value, 0, PageSize());

            dom.freeLists.pageCount--;
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
