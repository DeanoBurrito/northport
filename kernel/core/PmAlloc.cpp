#include <core/PmAlloc.h>
#include <core/Log.h>
#include <core/PmAccess.h>
#include <core/Config.h>
#include <Memory.h>

namespace Npk
{
    //There's no real reason for this, I just feel that this is a good limit
    //for a struct like this (which will exist for *every* usable page in
    //the system).
    static_assert(sizeof(PageInfo) <= sizeof(void*) * 4);

    sl::Opt<PageInfo*> PmAlloc(MemoryDomain& dom)
    {
        sl::ScopedLock scopeLock(dom.freeLists.lock);

        if (!dom.freeLists.zeroed.Empty())
        {
            dom.freeLists.pageCount--;
            return dom.freeLists.zeroed.PopFront();
        }

        if (!dom.freeLists.free.Empty())
        {
            dom.freeLists.pageCount--;
            auto page = dom.freeLists.free.PopFront();
            if (page->pm.count != 1)
            {
                auto nextPage = page++;
                nextPage->pm.count = page->pm.count - 1;
                dom.freeLists.free.PushBack(nextPage);
            }
            scopeLock.Release();

            auto pmaRef = Core::GetPmAccess(page);
            if (!pmaRef.Valid())
            {
                sl::ScopedLock scopeLock(dom.freeLists.lock);
                page->pm.count = 1;
                dom.freeLists.free.PushBack(page);

                return {};
            }

            sl::MemSet(pmaRef->value, 0, PageSize());
            return page;
        }

        return {};
    }

    void PmFree(MemoryDomain& dom, PageInfo* page)
    {
        VALIDATE_(page != nullptr, );

        sl::ScopedLock scopeLock(dom.freeLists.lock);
        page->pm.count = 1;
        dom.freeLists.free.PushBack(page);
        dom.freeLists.pageCount++;
    }
}
