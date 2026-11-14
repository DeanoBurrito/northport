#include <private/Vm.hpp>
#include <lib/PlacementNew.hpp>

namespace Npk::Private
{
    constexpr HeapTag BookFillTag = NPK_MAKE_HEAP_TAG("BFil");
    constexpr HeapTag BookCellTag = NPK_MAKE_HEAP_TAG("BCel");

    constexpr uint32_t ReadaheadDecayAt = 1024;
    constexpr uint32_t ReadaheadHistoryMin = 32;
    constexpr size_t ReadaheadScoreFull = 256;

    using BookSlot = sl::Atomic<BookEntry>;

    static_assert(BookFillLimit <= BookTrie::LeafFanout);

    struct OwnerView
    {
        Book* book;
        AnonPage* cell;
        BookSlot* slot;
        BookEntry value;
    };

    static void InitPageState(PageInfo* page, bool swapBacked,
        SwapHandle handle, uint16_t pins)
    {
        page->pins = pins;
        page->swapHandle = handle;

        PageClearFlag(page, PageVmFlag::Busy);
        PageClearFlag(page, PageVmFlag::Redirtied);
        PageClearFlag(page, PageVmFlag::ReadAhead);
        PageClearFlag(page, PageVmFlag::Mapped);

        if (handle != NoSwapHandle || !swapBacked)
            PageSetFlag(page, PageVmFlag::Clean);
        else
            PageClearFlag(page, PageVmFlag::Clean);
    }

    static void ResetBookPage(PageInfo* page, Book& book, size_t offset,
        SwapHandle handle, uint16_t pins)
    {
        page->offset = static_cast<uint32_t>(offset);

        InitPageState(page, book.backing == BookBacking::Swap, handle, pins);
        PageInfoSetVmOwner(page, &book, PageVmOwnerType::Book);
    }

    static void ClearPageOwner(PageInfo* page)
    {
        PageInfoSetVmOwner(page, nullptr, PageVmOwnerType::Book);
    }

    static bool TakeReadaheadMark(PageInfo* page)
    {
        if (!PageHasFlag(page, PageVmFlag::ReadAhead))
            return false;

        const uint16_t bit = 1 << static_cast<unsigned>(PageVmFlag::ReadAhead);
        const uint16_t prev = page->flags.FetchAnd(
            static_cast<uint16_t>(~bit), sl::Relaxed);

        return (prev & bit) != 0;
    }

    static void NoteReadaheadUse(Book& book, PageInfo* page)
    {
        if (!TakeReadaheadMark(page))
            return;

        book.readahead.used.FetchAdd(1, sl::Relaxed);
        if (book.backing == BookBacking::Swap)
            book.readahead.swapHits.FetchAdd(1, sl::Relaxed);
    }

    //must be in a rcu read section, locks the slot targeting `page`.
    //Returns whether the page has an owner.
    static bool LockOwnerSlot(OwnerView& out, PageInfo* page)
    {
        PageVmOwnerType type;
        void* owner = PageInfoGetVmOwner(type, page);

        while (true)
        {
            out = {};
            if (owner == nullptr)
                return false;

            BookSlot* slot;
            if (type == PageVmOwnerType::Cell)
            {
                out.cell = static_cast<AnonPage*>(owner);
                slot = &out.cell->word;
            }
            else
            {
                out.book = static_cast<Book*>(owner);
                slot = BookTrie::Lookup(out.book->root, page->offset);
            }

            BookEntry value = 0;
            if (slot != nullptr)
                value = LockBookEntry(*slot, true);

            if (value == MakeBookEntry(page))
            {
                out.slot = slot;
                out.value = value;

                return true;
            }
            if (value != 0)
                UnlockBookEntry(*slot, value);

            owner = PageInfoGetVmOwner(type, page);
        }
    }

    //must hold the owner's lock bit.
    static void UnlockOwnerSlot(const OwnerView& owner)
    {
        UnlockBookEntry(*owner.slot, owner.value);
    }

    static void PinPage(PageInfo* page)
    {
        constexpr auto PinsLimit = (1 << (sizeof(page->pins) * 8)) - 1;
        NPK_ASSERT(page->pins < PinsLimit);

        page->pins++;
    }

    static void UnpinPage(PageInfo* page, size_t count = 1)
    {
        NPK_ASSERT(page->pins >= count);

        page->pins -= count;
    }

    static void ClearPageMapped(PageInfo* page)
    {
        if (!PageHasFlag(page, PageVmFlag::Mapped))
            return;

        PageClearFlag(page, PageVmFlag::Mapped);
        UnpinPage(page);
    }

    //clears the `Clean` flag, returns and clears the swap handle for `page`.
    static SwapHandle AnonMarkDirty(PageInfo* page)
    {
        if (!PageHasFlag(page, PageVmFlag::Clean))
        {
            NPK_ASSERT(page->swapHandle == NoSwapHandle);

            return NoSwapHandle;
        }

        const SwapHandle handle = page->swapHandle;
        NPK_ASSERT(handle != NoSwapHandle);

        page->swapHandle = NoSwapHandle;
        PageClearFlag(page, PageVmFlag::Clean);

        return handle;
    }

    static SwapHandle MarkWrittenLocked(bool swapBacked, PageInfo* page)
    {
        if (!swapBacked)
        {
            PageClearFlag(page, PageVmFlag::Clean);

            return NoSwapHandle;
        }

        SwapHandle dead = NoSwapHandle;
        if (PageHasFlag(page, PageVmFlag::Clean))
            dead = AnonMarkDirty(page);

        const bool busy = PageHasFlag(page, PageVmFlag::Busy);
        if (busy)
            PageSetFlag(page, PageVmFlag::Redirtied);

        return dead;
    }

    static void FreeHandle(SwapHandle handle)
    {
        if (handle != NoSwapHandle)
            SwapFreeHandle(handle);
    }

    //must hold the lock bit for `value`.
    static void FoundLocked(BookLookup& out, BookEntry value, bool write,
        bool swapBacked, SwapHandle& dead)
    {
        switch (GetBookEntryType(value))
        {
        case BookEntryType::Page:
            out.result = BookFindResult::Page;
            out.page = BookEntryPage(value);

            PinPage(out.page);
            if (write)
                dead = MarkWrittenLocked(swapBacked, out.page);
            return;

        case BookEntryType::Swap:
            out.result = BookFindResult::Swapped;
            out.handle = BookEntrySwap(value);
            return;

        case BookEntryType::Marker:
            out.result = BookFindResult::Filling;
            out.fill = BookEntryFill(value);
            out.fill->refs.FetchAdd(1, sl::Relaxed);
            return;

        case BookEntryType::Cell:
            break;
        }

        NPK_UNREACHABLE();
    }

    //must NOT be holding the book's mutex.
    static NpkStatus GrowFor(Book& book, size_t offset)
    {
        Private::TrieSpares spares {};
        NpkStatus status = NpkStatus::Success;

        while (true)
        {
            auto result = AcquireSxMutexExclusive(&book.mutex, sl::NoTimeout,
                NPK_WAIT_LOCATION);
            NPK_ASSERT(result == NpkStatus::Success);

            const size_t needed = BookTrie::NodesNeeded(book.root, offset);
            if (needed <= spares.count)
            {
                if (needed != 0)
                {
                    auto* slot = BookTrie::Ensure(book.root, offset, spares);
                    NPK_ASSERT(slot != nullptr);
                }

                ReleaseSxMutexExclusive(&book.mutex);
                break;
            }

            ReleaseSxMutexExclusive(&book.mutex);

            status = BookTrie::FillSpares(spares, needed);
            if (status != NpkStatus::Success)
                break;
        }

        BookTrie::DrainSpares(spares);

        return status;
    }

    BookEntry LockBookEntry(BookSlot& slot, bool conditionalEmpty)
    {
        NPK_ASSERT(CurrentIpl() == Ipl::Dpc);

        while (true)
        {
            BookEntry value = slot.Load(sl::Acquire);

            if (conditionalEmpty && value == 0)
                return 0;
            if ((value & BookEntryLocked) != 0)
            {
                sl::HintSpinloop();
                continue;
            }

            const BookEntry locked = value | BookEntryLocked;
            if (slot.CompareExchange(value, locked, sl::Acquire))
                return value;
        }
    }

    void UnlockBookEntry(BookSlot& slot, BookEntry value)
    {
        NPK_ASSERT((value & (BookEntryLocked | BookEntryWaiters)) == 0);
        NPK_ASSERT((slot.Load(sl::Relaxed) & BookEntryLocked) != 0);

        slot.Store(value, sl::Release);
    }

    void ResetBook(Book& book, BookKind kind, BookBacking backing)
    {
        NPK_ASSERT(book.ranges.Empty());

        const auto result = ResetSxMutex(&book.mutex);
        NPK_ASSERT(result == NpkStatus::Success);

        book.root.Store(0, sl::Relaxed);
        book.removalsInFlight.Store(0, sl::Relaxed);
        book.readahead.issued.Store(0, sl::Relaxed);
        book.readahead.used.Store(0, sl::Relaxed);
        book.readahead.lastSwapPlusOne.Store(0, sl::Relaxed);
        book.readahead.swapHits.Store(0, sl::Relaxed);
        book.readahead.swapWindow.Store(0, sl::Relaxed);
        book.backing = backing;
        book.kind = kind;
    }

    void RegisterBookZones()
    {
        Private::RegisterExactPoolZone(Private::TrieNodeTag, BookTrie::NodeSize,
            Private::ZoneSharing::Packed);
        Private::RegisterExactPoolZone(BookFillTag, sizeof(BookFill),
            Private::ZoneSharing::Packed);
        Private::RegisterExactPoolZone(BookCellTag, sizeof(AnonSharerBlock),
            Private::ZoneSharing::Packed);
    }

    void FinishBook(Book& book)
    {
        NPK_ASSERT(book.ranges.Empty());

        size_t index;
        const auto* slot = BookTrie::Next(index, book.root, 0);
        NPK_ASSERT(slot == nullptr);

        BookTrie::Destroy(book.root);
    }

    Book* BookOwnerOf(PageInfo* page)
    {
        PageVmOwnerType type;
        void* owner = PageInfoGetVmOwner(type, page);
        if (type != PageVmOwnerType::Book)
            return nullptr;

        return static_cast<Book*>(owner);
    }

    NpkStatus BookPopulate(Book& book, size_t offset, PageInfo* fresh)
    {
        while (true)
        {
            auto result = AcquireSxMutexShared(&book.mutex, sl::NoTimeout,
                NPK_WAIT_LOCATION);
            NPK_ASSERT(result == NpkStatus::Success);

            auto section = RcuReadLock();
            BookSlot* slot = BookTrie::Lookup(book.root, offset);
            if (slot == nullptr)
            {
                RcuReadUnlock(section);
                ReleaseSxMutexShared(&book.mutex);

                const auto status = GrowFor(book, offset);
                if (status != NpkStatus::Success)
                    return status;

                continue;
            }

            const BookEntry value = LockBookEntry(*slot);
            if (value != 0)
            {
                UnlockBookEntry(*slot, value);
                RcuReadUnlock(section);
                ReleaseSxMutexShared(&book.mutex);

                return NpkStatus::Exists;
            }

            ResetBookPage(fresh, book, offset, NoSwapHandle, 1);

            UnlockBookEntry(*slot, MakeBookEntry(fresh));
            RcuReadUnlock(section);
            ReleaseSxMutexShared(&book.mutex);

            return NpkStatus::Success;
        }
    }

    BookLookup BookLookupPin(Book& book, size_t offset, bool write)
    {
        auto section = RcuReadLock();

        BookEntry value = 0;
        SwapHandle dead = NoSwapHandle;
        const bool swapBacked = book.backing == BookBacking::Swap;
        BookSlot* slot = BookTrie::Lookup(book.root, offset);

        BookLookup ret {};
        if (slot != nullptr)
            value = LockBookEntry(*slot, true);
        if (value == 0)
        {
            RcuReadUnlock(section);

            return ret;
        }

        if (AnonPage* cell = BookEntryCell(value); cell != nullptr)
        {
            const bool held = BookHoldCell(cell);
            NPK_ASSERT(held);

            const BookEntry inner = LockBookEntry(cell->word);
            ret.cell = cell;
            ret.shared = cell->sharerCount > 1;

            FoundLocked(ret, inner, write && !ret.shared, true, dead);
            UnlockBookEntry(cell->word, inner);
        }
        else
            FoundLocked(ret, value, write, swapBacked, dead);

        UnlockBookEntry(*slot, value);
        RcuReadUnlock(section);
        FreeHandle(dead);

        if (ret.result == BookFindResult::Page && ret.cell == nullptr)
            NoteReadaheadUse(book, ret.page);

        return ret;
    }

    bool BookHasEntry(Book& book, size_t offset)
    {
        auto section = RcuReadLock();

        BookSlot* slot = BookTrie::Lookup(book.root, offset);
        BookEntry value = 0;
        if (slot != nullptr)
            value = slot->Load(sl::Acquire);
        RcuReadUnlock(section);

        const bool isEmpty = BookEntryEmpty(value);

        return !isEmpty;
    }

    void BookMarkWritten(Book* book, PageInfo* page)
    {
        auto section = RcuReadLock();

        OwnerView owner;
        if (!LockOwnerSlot(owner, page))
        {
            NPK_ASSERT(book == nullptr);
            RcuReadUnlock(section);

            return;
        }

        if (book != nullptr)
        {
            NPK_ASSERT(owner.cell != nullptr || owner.book == book);
            NPK_ASSERT(page->pins != 0);
        }

        const bool anon = BookOwnerSwapBacked(owner.book, owner.cell);
        const SwapHandle dead = MarkWrittenLocked(anon, page);

        UnlockOwnerSlot(owner);
        RcuReadUnlock(section);
        FreeHandle(dead);
    }

    void BookCommit(Book& book, PageInfo* page)
    {
        auto result = AcquireSxMutexShared(&book.mutex, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        NPK_ASSERT(result == NpkStatus::Success);

        BookCommitLocked(book, page);
        ReleaseSxMutexShared(&book.mutex);
    }

    void BookCommitLocked(Book& book, PageInfo* page)
    {
        auto section = RcuReadLock();

        OwnerView owner;
        const bool owned = LockOwnerSlot(owner, page);
        NPK_ASSERT(owned);
        NPK_ASSERT(owner.cell != nullptr || owner.book == &book);

        if (owner.cell != nullptr)
            owner.cell->commitGeneration++;

        if (PageHasFlag(page, PageVmFlag::Mapped))
            UnpinPage(page);
        else
            PageSetFlag(page, PageVmFlag::Mapped);

        UnlockOwnerSlot(owner);
        RcuReadUnlock(section);
    }

    void BookUnpin(PageInfo* page)
    {
        auto section = RcuReadLock();

        OwnerView owner;
        const bool owned = LockOwnerSlot(owner, page);
        NPK_ASSERT(owned);

        UnpinPage(page);
        UnlockOwnerSlot(owner);
        RcuReadUnlock(section);
    }
}
