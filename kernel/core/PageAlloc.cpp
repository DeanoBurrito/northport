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

    //NOTE: assumes `dom.freeLists.lock` is held. 
    static bool PagesBecameAvailable(SystemDomain& dom, size_t before)
    {
        const size_t min = dom.pressure.minPages;

        return before <= min && dom.freeLists.pageCount > min;
    }

    //NOTE: assumes `dom.freeLists.lock` is held. 
    static PageInfo* TakePage(bool& zeroed, SystemDomain& dom)
    {
        if (dom.freeLists.pageCount <= dom.pressure.minPages)
            return nullptr;

        if (!dom.freeLists.zeroed.Empty())
        {
            zeroed = true;
            dom.freeLists.pageCount--;

            return dom.freeLists.zeroed.PopFront();
        }

        if (dom.freeLists.free.Empty())
            return nullptr;

        auto* page = dom.freeLists.free.PopFront();
        if (page->runLength > 1)
        {
            PageInfo* next = page + 1;
            next->runLength = page->runLength - 1;

            const auto success = dom.freeLists.free.PushBack(next);
            NPK_ASSERT(success);
        }

        zeroed = false;
        dom.freeLists.pageCount--;

        return page;
    }

    static bool ZeroPage(PageInfo* page)
    {
        const auto prevIpl = EnsureIpl(Ipl::Dpc);
        auto access = AccessPage(page);

        const bool valid = access.Valid();
        if (valid)
            sl::MemSet(access.vaddr, 0, PageSize());

        DestroyPageAccess(&access);
        RestoreIpl(prevIpl);

        return valid;
    }

    //NOTE: `*remaining` is the free count after the attempt, either way.
    static PageInfo* TakeZeroedPage(size_t& remaining, SystemDomain& dom)
    {
        bool zeroed = false;

        dom.freeLists.lock.Lock();
        auto* page = TakePage(zeroed, dom);
        remaining = dom.freeLists.pageCount;
        dom.freeLists.lock.Unlock();

        if (page == nullptr || zeroed)
            return page;
        if (ZeroPage(page))
            return page;

        //no window to zero it through, so it goes back as it was.
        dom.freeLists.lock.Lock();
        page->runLength = 1;

        const auto success = dom.freeLists.free.PushFront(page);
        NPK_ASSERT(success);
        dom.freeLists.pageCount++;
        dom.freeLists.lock.Unlock();

        return nullptr;
    }

    void Private::ReclaimLoaderMemory(PageList& list, size_t pageCount)
    {
        auto& dom = MySystemDomain();

        dom.freeLists.lock.Lock();
        const size_t before = dom.freeLists.pageCount;

        //NOTE: we dont overwrite `info->runLength` here since bringup has
        //set it to the real page count of the memory region. It's value is
        //legit and we need it untouched to be correct.
        while (!list.Empty())
        {
            auto* page = list.PopFront();
            const auto success = dom.freeLists.free.PushBack(page);
            NPK_ASSERT(success);
        }

        dom.freeLists.pageCount += pageCount;
        const bool available = PagesBecameAvailable(dom, before);
        dom.freeLists.lock.Unlock();

        if (available)
            SetConditionTo(&dom.pressure.pagesAvail, 0);

        auto conv = sl::ConvertUnits(pageCount << PfnShift());
        Log("Reclaimed %zu.%zu %sB of loader memory.", LogLevel::Info,
            conv.major, conv.minor, conv.prefix);
    }

    PageInfo* AllocPage(bool canFail)
    {
        auto& dom = MySystemDomain();

        size_t remaining;
        auto* page = TakeZeroedPage(remaining, dom);

        if (remaining < dom.pressure.lowPages)
            SetCondition(&dom.pressure.wakeTrimmer);

        if (page != nullptr || canFail)
            return page;
        NPK_ASSERT(CurrentIpl() == Ipl::Passive);

        for (size_t round = 0; round < AllocWaitRounds; round++)
        {
            AddPageDebt(1);
            WaitForPages({ sl::TimeScale::Millis, AllocWaitMs });

            page = TakeZeroedPage(remaining, dom);
            if (page != nullptr)
                return page;

            Log("Blocking page allocation waiting on reclaim, round %zu.",
                LogLevel::Trace, round);
        }

        Panic("Out of memory: no pages free after %zu attempts (%zums each).",
            nullptr, AllocWaitRounds, AllocWaitMs);
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

    void FreePage(PageInfo* page)
    {
        auto& dom = MySystemDomain();

        dom.freeLists.lock.Lock();
        const size_t before = dom.freeLists.pageCount;

        page->runLength = 1;
        const auto success = dom.freeLists.free.PushBack(page);
        NPK_ASSERT(success);
        dom.freeLists.pageCount++;

        const bool available = PagesBecameAvailable(dom, before);
        dom.freeLists.lock.Unlock();

        if (available)
            SetConditionTo(&dom.pressure.pagesAvail, 0);
    }

    void FreePageList(PageList& pages)
    {
        auto& dom = MySystemDomain();

        dom.freeLists.lock.Lock();
        const size_t before = dom.freeLists.pageCount;
        while (!pages.Empty())
        {
            auto* page = pages.PopFront();

            page->runLength = 1;
            const auto success = dom.freeLists.free.PushBack(page);
            NPK_ASSERT(success);
            dom.freeLists.pageCount++;
        }
        const bool available = PagesBecameAvailable(dom, before);
        dom.freeLists.lock.Unlock();

        if (available)
            SetConditionTo(&dom.pressure.pagesAvail, 0);
    }
}
