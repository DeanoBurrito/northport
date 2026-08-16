#include <Core.hpp>
#include <lib/Memory.hpp>

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
            auto* page = dom.freeLists.free.PopFront();

            if (page->pm.count > 1)
            {
                PageInfo* next = page + 1;
                next->pm.count = page->pm.count - 1;
                dom.freeLists.free.PushBack(next);
            }

            auto access = AccessPage(page);
            if (!access.Valid())
            {
                dom.freeLists.free.PushFront(page);

                return nullptr;
            }

            sl::MemSet(access.vaddr, 0, PageSize());
            dom.freeLists.pageCount--;

            return page;
        }

        return nullptr;
    }

    PageInfo* AllocPage(bool canFail)
    {
        auto& dom = MySystemDomain();

        dom.freeLists.lock.Lock();
        auto* page = TakePage(dom);
        dom.freeLists.lock.Unlock();

        if (page != nullptr || canFail)
            return page;

        NPK_UNREACHABLE(); //TODO: wait for a page to be available
    }

    void FreePage(PageInfo* page)
    {
        auto& dom = MySystemDomain();

        dom.freeLists.lock.Lock();
        page->pm.count = 1;
        dom.freeLists.free.PushBack(page);
        dom.freeLists.pageCount++;
        dom.freeLists.lock.Unlock();
    }

    void FreePageList(PageList& pages)
    {
        auto& dom = MySystemDomain();

        dom.freeLists.lock.Lock();
        while (!pages.Empty())
        {
            auto* page = pages.PopFront();

            page->pm.count = 1;
            dom.freeLists.free.PushBack(page);
            dom.freeLists.pageCount++;
        }
        dom.freeLists.lock.Unlock();
    }
}
