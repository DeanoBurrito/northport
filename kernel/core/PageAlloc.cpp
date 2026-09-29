#include <private/Core.hpp>
#include <lib/Memory.hpp>
#include <lib/Units.hpp>

namespace Npk
{
    void Private::ReclaimLoaderMemory(PageList& list, size_t pageCount)
    {
        auto& dom = MySystemDomain();

        dom.freeLists.lock.Lock();
        //NOTE: we dont overwrite `info->pm.count` here since bringup has
        //set it to the real page count of the memory region. It's value is
        //legit and we need it untouched to be correct.
        while (!list.Empty())
            dom.freeLists.free.PushBack(list.PopFront());
        dom.freeLists.pageCount += pageCount;

        dom.freeLists.lock.Unlock();

        auto conv = sl::ConvertUnits(pageCount << PfnShift());
        Log("Reclaimed %zu.%zu %sB of loader memory.", LogLevel::Info,
            conv.major, conv.minor, conv.prefix);
    }

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

            //this should be true by the caller holding the ipl spinlock
            //protecting the freelists, but this assert catches us if that lock
            //ever changes implementation. The IPL is required for using
            //AccessPage() below.
            AssertIpl(Ipl::Dpc);
            auto access = AccessPage(page);
            if (!access.Valid())
            {
                page->pm.count = 1;
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
