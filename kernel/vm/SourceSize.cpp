#include <private/Vm.hpp>
#include <lib/Maths.hpp>

namespace Npk::Private
{
    constexpr size_t UnmapBatch = 16;
    constexpr size_t DropRetries = 500;
    constexpr sl::TimeCount DropPause = 2_ms;

    static bool truncateUnmapKept = true;

    static void PublishExtent(VmSource& src, uint64_t size,
        uint64_t initialized, bool bounded)
    {
        src.sizeLock.Lock();
        src.sizeSeq.BeginWrite();

        src.size = size;
        src.initializedSize = initialized;
        src.bounded = bounded;

        src.sizeSeq.EndWrite();
        src.sizeLock.Unlock();
    }

    static void PauseForDrop()
    {
        Condition never {};
        ResetCondition(&never, 1);

        WaitEntry entry {};
        WaitOne(&never, &entry, DropPause, NPK_WAIT_LOCATION);
    }

    static void ZeroResidentFrom(VmSource& src, uint64_t at)
    {
        const size_t within = static_cast<size_t>(at & PageMask());
        if (within == 0)
            return;

        while (true)
        {
            const auto found = BookLookupPin(src.book,
                static_cast<size_t>(at >> PfnShift()), false);
            NPK_ASSERT(found.cell == nullptr);

            if (found.result == BookFindResult::Filling)
            {
                BookWaitFill(found.fill);
                continue;
            }
            if (found.result != BookFindResult::Page)
                return;

            ZeroPageFrom(found.page, within);
            BookUnpin(found.page);

            return;
        }
    }

    //NOTE: book is locked in exclusive mode.
    static void UnmapEverywhere(Book& book, PageInfo* page)
    {
        if (BookOwnerOf(page) != &book)
            return;

        const Paddr frame = LookupPagePaddr(page);
        const size_t index = page->offset;
        bool dirtied = false;

        for (auto it = book.ranges.Begin(); it != book.ranges.End(); ++it)
        {
            const BookSpan link = BookLinkSpan(book, *it);
            if (index < link.begin || index >= link.end)
                continue;

            VmRange& range = *it->range;
            HwMap* map = range.space->map;
            if (map == nullptr)
                continue;

            const uintptr_t addr = range.base
                + ((index - link.begin) << PfnShift());
            Paddr present;
            if (HwMapExtract(&present, nullptr, nullptr, map, addr)
                != NpkStatus::Success || present != frame)
                continue;

            MapBatchRange store[1 + MapBatchTableSlack];
            MapBatch batch {};
            ResetMapBatch(batch, map, store);

            BeginMappingRemovals(range);
            const auto harvest = HwMapHarvest(batch, addr, frame,
                HwHarvestAction::Remove, {});
            if (harvest.removed)
            {
                HwMapUpdate(batch, true);
                dirtied = dirtied || harvest.state.Has(MmuPageState::Dirty);
            }

            FinishMapBatch(batch);
            EndMappingRemovals(range);
        }

        //a fault replacing an entry keeps the pin until it is done.
        const bool forget = PageHasFlag(page, PageVmFlag::Mapped)
            && book.removalsInFlight.Load(sl::Acquire) == 0;
        if (dirtied || forget)
            BookSettleSweep(book, page, dirtied, forget);
    }

    static void UnmapPastEnd(VmSource& src, size_t firstPage)
    {
        Book& book = src.book;
        const auto locked = AcquireSxMutexExclusive(&book.mutex,
            sl::NoTimeout, NPK_WAIT_LOCATION);
        NPK_ASSERT(locked == NpkStatus::Success);

        size_t cursor = firstPage;
        bool more = true;
        while (more)
        {
            PageInfo* pages[UnmapBatch];
            size_t found = 0;

            auto section = RcuReadLock();
            while (found < UnmapBatch)
            {
                size_t index;
                auto* slot = BookTrie::Next(index, book.root, cursor);
                if (slot == nullptr)
                {
                    more = false;
                    break;
                }

                cursor = index + 1;
                PageInfo* page = BookEntryPage(slot->Load(sl::Relaxed));
                if (page != nullptr)
                    pages[found++] = page;
            }
            RcuReadUnlock(section);

            for (size_t i = 0; i < found; i++)
                UnmapEverywhere(book, pages[i]);
        }

        ReleaseSxMutexExclusive(&book.mutex);
    }

    static bool NextEntry(size_t& index, Book& book, size_t from)
    {
        auto section = RcuReadLock();
        auto* slot = BookTrie::Next(index, book.root, from);

        bool found = false;
        while (slot != nullptr && !found)
        {
            found = slot->Load(sl::Relaxed) != 0;
            if (!found)
                slot = BookTrie::Next(index, book.root, index + 1);
        }
        RcuReadUnlock(section);

        return found;
    }

    static NpkStatus DropPastEnd(VmSource& src, size_t firstPage)
    {
        size_t cursor = firstPage;
        size_t tries = 0;

        size_t index;
        while (NextEntry(index, src.book, cursor))
        {
            if (SourcePageDrop(&src, index) == NpkStatus::Success)
            {
                cursor = index + 1;
                continue;
            }

            if (tries == DropRetries)
                return NpkStatus::Busy;

            tries++;
            PauseForDrop();
            cursor = index;
        }

        return NpkStatus::Success;
    }

    static NpkStatus ClearPastEnd(VmSource& src, uint64_t size)
    {
        const uint64_t first = PageCountFor(size);
        if (first > static_cast<size_t>(-1))
            return NpkStatus::Success;

        if (truncateUnmapKept)
            UnmapPastEnd(src, static_cast<size_t>(first));

        const auto dropped = DropPastEnd(src, static_cast<size_t>(first));
        ZeroResidentFrom(src, size);

        return dropped;
    }
}

namespace Npk
{
    using namespace Private;

    VmSourceSize GetSourceSize(const VmSource* source)
    {
        VmSourceSize out {};

        while (true)
        {
            const uint32_t token = source->sizeSeq.BeginRead();

            out.size = source->size;
            out.initializedSize = source->initializedSize;
            out.bounded = source->bounded;

            if (!source->sizeSeq.ReadRetry(token))
                break;
            sl::HintSpinloop();
        }

        return out;
    }

    NpkStatus ResetSourceSize(VmSource* source, uint64_t size,
        uint64_t initializedSize, bool bounded)
    {
        NPK_CHECK(source != nullptr, NpkStatus::InvalidArg);
        AssertIpl(Ipl::Passive);

        const auto now = GetSourceSize(source);
        if (!bounded)
        {
            if (!now.bounded)
                return NpkStatus::Success;

            ZeroResidentFrom(*source, now.size);
            PublishExtent(*source, 0, 0, false);

            return NpkStatus::Success;
        }

        NPK_CHECK(initializedSize <= size, NpkStatus::InvalidArg);
        NPK_CHECK(Private::BookBytesFit(0, size), NpkStatus::TooLarge);
        if (now.bounded)
            return NpkStatus::InUse;

        PublishExtent(*source, size, initializedSize, true);
        ZeroResidentFrom(*source, initializedSize);

        return ClearPastEnd(*source, size);
    }

    NpkStatus SetSourceSize(VmSource* source, uint64_t size)
    {
        NPK_CHECK(source != nullptr, NpkStatus::InvalidArg);
        NPK_CHECK(Private::BookBytesFit(0, size), NpkStatus::TooLarge);
        AssertIpl(Ipl::Passive);

        const auto now = GetSourceSize(source);
        NPK_CHECK(now.bounded, NpkStatus::InvalidArg);

        if (size > now.size)
        {
            PublishExtent(*source, size, now.initializedSize, true);
            ZeroResidentFrom(*source, now.size);

            return NpkStatus::Success;
        }

        PublishExtent(*source, size, sl::Min(now.initializedSize, size), true);

        return ClearPastEnd(*source, size);
    }

    NpkStatus SetSourceInitializedSize(VmSource* source,
        uint64_t initializedSize)
    {
        NPK_CHECK(source != nullptr, NpkStatus::InvalidArg);

        const auto now = GetSourceSize(source);
        NPK_CHECK(now.bounded, NpkStatus::InvalidArg);
        NPK_CHECK(initializedSize <= now.size
            && initializedSize >= now.initializedSize, NpkStatus::InvalidArg);

        PublishExtent(*source, now.size, initializedSize, true);

        return NpkStatus::Success;
    }
}
