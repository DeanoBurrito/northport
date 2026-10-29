#include <private/Core.hpp>
#include <lib/Memory.hpp>
#include <lib/Units.hpp>

namespace Npk
{
    constexpr size_t AllocWaitRounds = 16;
    constexpr size_t AllocWaitMs = 250;

    static size_t FreePagesApprox(SystemDomain& dom)
    {
        return dom.freeLists.pageCount;
    }

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
        const size_t remaining = dom.freeLists.pageCount;
        dom.freeLists.lock.Unlock();

        if (page != nullptr && remaining < dom.pressure.lowPages)
            SetCondition(&dom.pressure.wakeTrimmer);

        if (page != nullptr || canFail)
            return page;
        NPK_ASSERT(CurrentIpl() == Ipl::Passive);

        for (size_t round = 0; round < AllocWaitRounds; round++)
        {
            AddPageDebt(1);
            WaitForPages(sl::TimeCount(sl::TimeScale::Millis, AllocWaitMs));

            dom.freeLists.lock.Lock();
            page = TakePage(dom);
            dom.freeLists.lock.Unlock();

            if (page != nullptr)
                return page;

            Log("Blocking page allocation waiting on reclaim, round %zu.",
                LogLevel::Trace, round);
        }
        
        Panic("Out of memory: no pages free after %zu attempts (%zums each).",
            nullptr, AllocWaitRounds, AllocWaitMs);
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

    MemoryPressure GetMemoryPressure()
    {
        auto& dom = MySystemDomain();

        dom.freeLists.lock.Lock();
        const size_t free = dom.freeLists.pageCount;
        dom.freeLists.lock.Unlock();

        return
        {
            .freePages = free,
            .minPages = dom.pressure.minPages,
            .lowPages = dom.pressure.lowPages,
            .highPages = dom.pressure.highPages,
            .debtPages = dom.pressure.debt.Load(sl::Relaxed),
        };
    }

    bool MemoryIsLow()
    {
        auto& dom = MySystemDomain();
        const auto free = FreePagesApprox(dom);
        const bool debt = dom.pressure.debt.Load(sl::Relaxed);

        return free < dom.pressure.lowPages || debt != 0;
    }

    bool MemoryIsCritical()
    {
        auto& dom = MySystemDomain();
        const auto free = FreePagesApprox(dom);

        return free < dom.pressure.minPages;
    }

    void AddPageDebt(size_t pages)
    {
        auto& dom = MySystemDomain();

        dom.pressure.debt.Add(pages, sl::Relaxed);
        SetCondition(&dom.pressure.wakeTrimmer);
    }

    NpkStatus WaitForPages(sl::TimeCount timeout)
    {
        auto& dom = MySystemDomain();
        auto* cond = &dom.pressure.pagesAvail;

        WaitEntry entry {};
        const auto result = WaitOne(cond, &entry, timeout, NPK_WAIT_LOCATION);

        if (result == NpkStatus::Timeout || result == NpkStatus::Reset)
            return NpkStatus::Success;

        return result;
    }
}
