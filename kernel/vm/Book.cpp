#include <private/Vm.hpp>
#include <lib/PlacementNew.hpp>

namespace Npk::Private
{
    constexpr HeapTag BookFillTag = NPK_MAKE_HEAP_TAG("BFil");
    constexpr HeapTag BookCellTag = NPK_MAKE_HEAP_TAG("BCel");

    constexpr size_t RemoveBatchSize = 16;
    constexpr uint32_t ReadaheadDecayAt = 1024;
    constexpr uint32_t ReadaheadHistoryMin = 32;
    constexpr size_t ReadaheadScoreFull = 256;

    using BookSlot = sl::Atomic<BookEntry>;

    static_assert(BookFillLimit <= BookTrie::LeafFanout);

    struct SlotCursor
    {
        BookSlot* slot;
        size_t run;
        size_t index;
    };

    struct OwnerView
    {
        Book* book;
        AnonCell* cell;
        BookSlot* slot;
        BookEntry value;
    };

    struct RemovalBatch
    {
        PageInfo* pages[RemoveBatchSize];
        SwapHandle handles[RemoveBatchSize];
        size_t pageCount;
        size_t handleCount;
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
        NPK_ASSERT(offset < BookPageLimit);
        page->offset = static_cast<BookIndex>(offset);

        InitPageState(page, book.backing == BookBacking::Swap, handle, pins);
        PageInfoSetVmOwner(page, &book, PageVmOwnerType::Book);
    }

    static void ResetCellPage(PageInfo* page, AnonCell* cell,
        SwapHandle handle, uint16_t pins)
    {
        InitPageState(page, true, handle, pins);
        PageInfoSetVmOwner(page, cell, PageVmOwnerType::Cell);
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

    static void NoteReadaheadIssued(Book& book, uint32_t count)
    {
        if (count == 0)
            return;

        auto& ra = book.readahead;
        const auto issued = ra.issued.FetchAdd(count, sl::Relaxed) + count;
        if (issued < ReadaheadDecayAt)
            return;

        ra.issued.Store(issued / 2, sl::Relaxed);
        ra.used.Store(ra.used.Load(sl::Relaxed) / 2, sl::Relaxed);
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
                out.cell = static_cast<AnonCell*>(owner);
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

    //must hold the cell's lock bit.
    static AnonSharer& SharerAt(AnonCell* cell, size_t index)
    {
        if (index < CellInlineSharers)
            return cell->inlineSharers[index];

        index -= CellInlineSharers;
        AnonSharerBlock* block = cell->moreSharers;

        while (index >= CellBlockSharers)
        {
            block = block->next;
            index -= CellBlockSharers;
        }

        return block->sharers[index];
    }

    //must hold the cell's lock bit.
    static bool CellFull(const AnonCell* cell)
    {
        const size_t count = cell->sharerCount;

        return count >= CellInlineSharers
            && (count - CellInlineSharers) % CellBlockSharers == 0;
    }

    //must hold the cell's lock bit.
    static void AddSharer(AnonCell* cell, AnonSharer sharer,
        AnonSharerBlock*& spares)
    {
        if (CellFull(cell))
        {
            AnonSharerBlock* block = spares;
            NPK_ASSERT(block != nullptr);

            spares = block->next;
            block->next = nullptr;

            AnonSharerBlock** tail = &cell->moreSharers;
            while (*tail != nullptr)
                tail = &(*tail)->next;
            *tail = block;
        }

        SharerAt(cell, cell->sharerCount) = sharer;
        cell->sharerCount++;
        cell->sharerGeneration++;
    }

    //must hold the cell's lock bit. Returns the removed block (if any), for
    //the caller to free.
    static AnonSharerBlock* RemoveSharer(AnonCell* cell, Book* book,
        size_t offset)
    {
        const size_t count = cell->sharerCount;

        size_t found = count;
        for (size_t i = 0; i < count; i++)
        {
            const auto& sharer = SharerAt(cell, i);
            if (sharer.book == book && sharer.offset == offset)
            {
                found = i;
                break;
            }
        }
        NPK_ASSERT(found < count);

        const size_t last = count - 1;
        SharerAt(cell, found) = SharerAt(cell, last);
        cell->sharerCount = static_cast<uint32_t>(last);
        cell->sharerGeneration++;

        if (last < CellInlineSharers
            || (last - CellInlineSharers) % CellBlockSharers != 0)
        {
            return nullptr;
        }

        AnonSharerBlock** link = &cell->moreSharers;
        while ((*link)->next != nullptr)
            link = &(*link)->next;

        AnonSharerBlock* empty = *link;
        *link = nullptr;

        return empty;
    }

    static void FreeSharerBlock(AnonSharerBlock* block)
    {
        if (block != nullptr)
            PoolFreeWired(block, sizeof(AnonSharerBlock), BookCellTag);
    }

    static void FreeCellNow(EbrItem* item)
    {
        PoolFreeWired(item, sizeof(AnonCell), BookCellTag);
    }

    static void PushChain(void*& head, void* item)
    {
        *static_cast<void**>(item) = head;
        head = item;
    }

    static void* PopChain(void*& head)
    {
        void* item = head;
        if (item != nullptr)
        {
            head = *static_cast<void**>(item);
            *static_cast<void**>(item) = nullptr;
        }

        return item;
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

    //must hold the lock bit of the slot that names `value`.
    static void TakeContent(RemovalBatch& take, BookEntry value, size_t index)
    {
        switch (GetBookEntryType(value))
        {
        case BookEntryType::Page:
        {
            PageInfo* page = BookEntryPage(value);
            ClearPageMapped(page);
            NPK_ASSERT(page->pins == 0);

            const bool busy = PageHasFlag(page, PageVmFlag::Busy);
            if (busy)
            {
                NPK_ASSERT(page->swapHandle == NoSwapHandle);

                ClearPageOwner(page);
                break;
            }

            if (page->swapHandle != NoSwapHandle)
                take.handles[take.handleCount++] = page->swapHandle;
            page->swapHandle = NoSwapHandle;

            PageClearFlag(page, PageVmFlag::Clean);
            ClearPageOwner(page);
            take.pages[take.pageCount++] = page;
            break;
        }

        case BookEntryType::Swap:
            take.handles[take.handleCount++] = BookEntrySwap(value);
            break;

        case BookEntryType::Marker:
        {
            BookFill* fill = BookEntryFill(value);
            const size_t i = index - fill->first;
            NPK_ASSERT(i < fill->count);

            if (fill->handles[i] != NoSwapHandle)
                take.handles[take.handleCount++] = fill->handles[i];
            break;
        }

        case BookEntryType::Cell:
            NPK_UNREACHABLE();
        }
    }

    static void TopUpNodes(Private::TrieSpares& spares, BookPromoteSpares& from)
    {
        constexpr auto cap = Private::TrieSpares::Capacity;

        while (spares.count < cap && from.nodeCount != 0)
        {
            spares.nodes[spares.count++] = PopChain(from.nodes);
            from.nodeCount--;
        }
    }

    static void ReturnNodes(Private::TrieSpares& spares, BookPromoteSpares& to)
    {
        while (spares.count != 0)
        {
            PushChain(to.nodes, spares.nodes[--spares.count]);
            to.nodeCount++;
        }
    }

    static size_t LeafStop(size_t index, size_t end)
    {
        const size_t leafEnd = (index | (BookTrie::LeafFanout - 1)) + 1;

        return end < leafEnd ? end : leafEnd;
    }

    /* CAller must hold `from` exclusively. Populates `need` with a conservative
     * estimate of what resources a promote op would need.
     */
    static void CountPromoteNeed(BookPromoteNeed& need, BookSpan span,
        size_t first)
    {
        Book& from = *span.book;
        size_t cursor = span.begin;
        const size_t end = span.end;

        need = {};
        size_t leaves = 0;
        size_t lastLeaf = ~static_cast<size_t>(0);

        while (cursor < end)
        {
            auto section = RcuReadLock();

            size_t index;
            BookSlot* slot = BookTrie::Next(index, from.root, cursor);
            if (slot == nullptr || index >= end)
            {
                RcuReadUnlock(section);
                break;
            }

            const size_t stop = LeafStop(index, end);
            for (; index < stop; index++, slot++)
            {
                const BookEntry value = LockBookEntry(*slot, true);
                if (value == 0)
                    continue;

                const size_t leaf = (index - first) >> BookTrie::LeafBits;
                if (leaf != lastLeaf)
                {
                    leaves++;
                    lastLeaf = leaf;
                }

                if (AnonCell* cell = BookEntryCell(value); cell != nullptr)
                {
                    const BookEntry inner = LockBookEntry(cell->word);
                    if (CellFull(cell))
                        need.blocks++;

                    UnlockBookEntry(cell->word, inner);
                }
                else
                    need.cells++;

                UnlockBookEntry(*slot, value);
            }
            RcuReadUnlock(section);
            cursor = stop;
        }

        need.nodes = leaves * BookTrie::MaxHeight;
    }

    /* Must hold `book` exclusively. Moves `page` out of `cell` into the named
     * offset into `book`. Collapses a single owner cell into the entry
     * naming the page directly.
     * If success is returned the caller must drop the entry's reference to the
     * cell, after releasing the book mutex.
     */
    static bool UnpromoteLocked(Book& book, size_t offset, AnonCell* cell,
        PageInfo* page)
    {
        auto section = RcuReadLock();

        bool done = false;
        BookSlot* slot = BookTrie::Lookup(book.root, offset);
        BookEntry value = 0;
        if (slot != nullptr)
            value = LockBookEntry(*slot, true);

        const bool entryLocked = value != 0;
        if (value == MakeBookEntry(cell))
        {
            BookEntry inner = LockBookEntry(cell->word);

            if (inner == MakeBookEntry(page) && cell->sharerCount == 1)
            {
                AnonSharerBlock* empty = RemoveSharer(cell, &book, offset);
                NPK_ASSERT(empty == nullptr);

                NPK_ASSERT(offset < BookPageLimit);
                page->offset = static_cast<BookIndex>(offset);
                PageInfoSetVmOwner(page, &book, PageVmOwnerType::Book);

                inner = 0;
                value = MakeBookEntry(page);
                done = true;
            }

            UnlockBookEntry(cell->word, inner);
        }

        if (entryLocked)
            UnlockBookEntry(*slot, value);
        RcuReadUnlock(section);

        return done;
    }

    static void FreeFillNow(EbrItem* item)
    {
        auto* fill = reinterpret_cast<BookFill*>(item);

        PoolFreeWired(fill, sizeof(BookFill), BookFillTag);
    }

    static BookFill* AllocFill(size_t first)
    {
        void* mem = PoolAllocWired(sizeof(BookFill), BookFillTag);
        if (mem == nullptr)
            return nullptr;

        auto* fill = static_cast<BookFill*>(mem);
        fill->refs.Store(1, sl::Relaxed);
        fill->first = first;

        const auto result = ResetCondition(&fill->done, 1);
        NPK_ASSERT(result == NpkStatus::Success);

        return fill;
    }

    static void DropFill(BookFill* fill)
    {
        if (fill->refs.FetchSub(1, sl::AcqRel) != 1)
            return;

        fill->retire.callback = FreeFillNow;
        const auto result = RcuCall(&fill->retire);
        NPK_ASSERT(result == NpkStatus::Success);
    }

    static void FinishFill(BookFill* fill)
    {
        SetCondition(&fill->done);
        DropFill(fill);
    }

    static void FreeUnpublished(PageInfo* page)
    {
        ClearPageOwner(page);
        page->swapHandle = NoSwapHandle;
        page->pins = 0;

        FreePage(page);
    }

    static BookSlot* CursorAt(SlotCursor& cursor, Book& book, size_t index)
    {
        if (cursor.slot != nullptr && index == cursor.index + 1
            && cursor.run > 1)
        {
            cursor.slot++;
            cursor.run--;
            cursor.index = index;

            return cursor.slot;
        }

        cursor.index = index;
        cursor.slot = BookTrie::Lookup(book.root, index, &cursor.run);

        return cursor.slot;
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

        if (AnonCell* cell = BookEntryCell(value); cell != nullptr)
        {
            const bool held = BookTryRefCell(cell);
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

    NpkStatus BookBeginFill(BookFill*& out, BookSpan span, size_t mainIndex)
    {
        Book& book = *span.book;
        const size_t offset = span.begin;
        const size_t count = span.end - span.begin;

        NPK_ASSERT(count != 0 && count <= BookFillLimit);
        NPK_ASSERT(mainIndex < count);

        const size_t main = offset + mainIndex;
        const size_t last = offset + count - 1;
        const size_t mainLeaf = main >> BookTrie::LeafBits;

        size_t ahead = main;
        if ((offset >> BookTrie::LeafBits) != mainLeaf)
            ahead = offset;
        else if ((last >> BookTrie::LeafBits) != mainLeaf)
            ahead = last;

        BookFill* fill = AllocFill(main);
        if (fill == nullptr)
            return NpkStatus::Shortage;
        const BookEntry marker = MakeBookEntry(fill);

        while (true)
        {
            auto result = AcquireSxMutexShared(&book.mutex, sl::NoTimeout,
                NPK_WAIT_LOCATION);
            NPK_ASSERT(result == NpkStatus::Success);

            auto section = RcuReadLock();

            BookSlot* mainSlot = BookTrie::Lookup(book.root, main);
            const bool growAhead = ahead != main
                && BookTrie::Lookup(book.root, ahead) == nullptr;

            if (mainSlot == nullptr || growAhead)
            {
                RcuReadUnlock(section);
                ReleaseSxMutexShared(&book.mutex);

                if (mainSlot == nullptr)
                {
                    const auto status = GrowFor(book, main);
                    if (status != NpkStatus::Success)
                    {
                        FinishFill(fill);

                        return status;
                    }
                }

                if (growAhead)
                    (void)GrowFor(book, ahead);
                ahead = main;

                continue;
            }

            const BookEntry value = LockBookEntry(*mainSlot);
            if (value != 0)
            {
                UnlockBookEntry(*mainSlot, value);
                RcuReadUnlock(section);
                ReleaseSxMutexShared(&book.mutex);
                FinishFill(fill);

                return NpkStatus::Exists;
            }
            UnlockBookEntry(*mainSlot, marker);

            //fill the main entry first!
            size_t low = main;
            while (low > offset)
            {
                BookSlot* slot = BookTrie::Lookup(book.root, low - 1);
                if (slot == nullptr)
                    break;

                const BookEntry prev = LockBookEntry(*slot);
                if (prev != 0)
                {
                    UnlockBookEntry(*slot, prev);
                    break;
                }
                UnlockBookEntry(*slot, marker);
                low--;
            }

            SlotCursor cursor {};
            size_t high = main + 1;
            while (high <= last)
            {
                BookSlot* slot = CursorAt(cursor, book, high);
                if (slot == nullptr)
                    break;

                const BookEntry next = LockBookEntry(*slot);
                if (next != 0)
                {
                    UnlockBookEntry(*slot, next);
                    break;
                }
                UnlockBookEntry(*slot, marker);
                high++;
            }

            fill->first = low;
            fill->count = high - low;
            RcuReadUnlock(section);
            ReleaseSxMutexShared(&book.mutex);

            break;
        }

        out = fill;

        return NpkStatus::Success;
    }

    uint32_t BookEndFill(Book& book, BookFill* fill,
        sl::Span<PageInfo*> pages, uint32_t ahead)
    {
        NPK_ASSERT(pages.Size() == fill->count);

        const BookEntry marker = MakeBookEntry(fill);
        uint32_t published = 0;
        bool unfilled = false;

        auto result = AcquireSxMutexShared(&book.mutex, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        NPK_ASSERT(result == NpkStatus::Success);

        auto section = RcuReadLock();

        SlotCursor cursor {};
        for (size_t i = 0; i < fill->count; i++)
        {
            BookSlot* slot = CursorAt(cursor, book, fill->first + i);
            BookEntry value = 0;
            if (slot != nullptr)
                value = LockBookEntry(*slot, true);
            if (value != marker)
            {
                if (value != 0)
                    UnlockBookEntry(*slot, value);
                continue;
            }

            PageInfo* page = pages[i];
            if (page == nullptr || !PageHasFlag(page, PageVmFlag::Filled))
            {
                UnlockBookEntry(*slot, value);
                unfilled = true;
                continue;
            }

            ResetBookPage(page, book, fill->first + i, NoSwapHandle, 1);
            if ((ahead & (1u << i)) != 0)
                PageSetFlag(page, PageVmFlag::ReadAhead);

            UnlockBookEntry(*slot, MakeBookEntry(page));
            published |= 1u << i;
        }
        RcuReadUnlock(section);
        ReleaseSxMutexShared(&book.mutex);
        NoteReadaheadIssued(book, sl::PopCount(published & ahead));

        if (unfilled)
        {
            result = AcquireSxMutexExclusive(&book.mutex, sl::NoTimeout,
                NPK_WAIT_LOCATION);
            NPK_ASSERT(result == NpkStatus::Success);

            section = RcuReadLock();

            for (size_t i = 0; i < fill->count; i++)
            {
                if ((published & (1u << i)) != 0)
                    continue;

                auto* slot = BookTrie::Lookup(book.root, fill->first + i);
                BookEntry value = 0;

                if (slot != nullptr)
                    value = LockBookEntry(*slot, true);
                if (value == marker)
                    UnlockBookEntry(*slot, 0);
                else if (value != 0)
                    UnlockBookEntry(*slot, value);
            }
            RcuReadUnlock(section);

            //we covered at most 2 leaves (accounting for readahead).
            BookTrie::Prune(book.root, fill->first);
            BookTrie::Prune(book.root, fill->first + fill->count - 1);
            ReleaseSxMutexExclusive(&book.mutex);
        }

        for (size_t i = 0; i < fill->count; i++)
        {
            if ((published & (1u << i)) != 0 || pages[i] == nullptr)
                continue;

            FreeUnpublished(pages[i]);
        }

        FinishFill(fill);

        return published;
    }

    void BookWaitFill(BookFill* fill)
    {
        WaitEntry entry {};
        const auto result = WaitOne(&fill->done, &entry, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        NPK_ASSERT(result == NpkStatus::Success);

        DropFill(fill);
    }

    void BookLeaveFill(BookFill* fill)
    {
        DropFill(fill);
    }

    void BookNoteReadaheadUse(PageInfo* page)
    {
        if (!PageHasFlag(page, PageVmFlag::ReadAhead))
            return;

        auto section = RcuReadLock();
        auto* owner = BookOwnerOf(page);

        if (owner != nullptr)
            NoteReadaheadUse(*owner, page);
        else
            TakeReadaheadMark(page);
        RcuReadUnlock(section);
    }

    size_t BookReadaheadScore(const Book& book)
    {
        const uint32_t issued = book.readahead.issued.Load(sl::Relaxed);
        const uint32_t used = book.readahead.used.Load(sl::Relaxed);

        if (issued < ReadaheadHistoryMin)
            return ReadaheadScoreFull;

        const size_t score = (used * ReadaheadScoreFull) / issued;

        return sl::Min(score, ReadaheadScoreFull);
    }

    bool BookTryRefCell(AnonCell* cell)
    {
        while (true)
        {
            size_t refs = cell->refs.Load(sl::Relaxed);

            if (refs == 0)
                return false;
            if (cell->refs.CompareExchange(refs, refs + 1, sl::Acquire))
                return true;
        }
    }

    void BookUnrefCell(AnonCell* cell)
    {
        if (cell->refs.FetchSub(1, sl::AcqRel) != 1)
            return;

        NPK_ASSERT(cell->sharerCount == 0);
        NPK_ASSERT(cell->moreSharers == nullptr);
        NPK_ASSERT(cell->word.Load(sl::Relaxed) == 0);

        cell->retire.callback = FreeCellNow;
        const auto result = RcuCall(&cell->retire);

        NPK_ASSERT(result == NpkStatus::Success);
    }

    BookBreak BreakCowOnBookCell(Book& book, size_t offset, AnonCell* cell,
        PageInfo* old, PageInfo* fresh)
    {
        auto result = AcquireSxMutexShared(&book.mutex, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        NPK_ASSERT(result == NpkStatus::Success);

        auto section = RcuReadLock();

        BookSlot* slot = BookTrie::Lookup(book.root, offset);
        BookEntry value = 0;
        if (slot != nullptr)
            value = LockBookEntry(*slot, true);

        auto outcome = BookBreak::TryAgain;
        SwapHandle dead = NoSwapHandle;
        AnonSharerBlock* empty = nullptr;
        const bool locked = value != 0;

        if (value == MakeBookEntry(cell))
        {
            const auto inner = LockBookEntry(cell->word);

            if (inner != MakeBookEntry(old))
                outcome = BookBreak::TryAgain;
            else if (cell->sharerCount == 1)
            {
                dead = MarkWrittenLocked(true, old);
                outcome = BookBreak::ReusedOld;
            }
            else
            {
                UnpinPage(old);
                empty = RemoveSharer(cell, &book, offset);
                ResetBookPage(fresh, book, offset, NoSwapHandle, 1);
                value = MakeBookEntry(fresh);

                outcome = BookBreak::UsedFresh;
            }

            UnlockBookEntry(cell->word, inner);
        }

        if (locked)
            UnlockBookEntry(*slot, value);
        RcuReadUnlock(section);
        ReleaseSxMutexShared(&book.mutex);

        FreeHandle(dead);
        FreeSharerBlock(empty);

        if (outcome == BookBreak::UsedFresh)
            BookUnrefCell(cell);
        else if (outcome == BookBreak::ReusedOld)
            BookTryUnpromote(book, offset, cell, old);

        return outcome;
    }

    NpkStatus BookPromote(BookSpan& span, size_t first, Book& to,
        BookPromoteSpares& spares, BookPromoteNeed& need, BookFill*& fill)
    {
        Book& from = *span.book;
        size_t cursor = span.begin;
        const size_t end = span.end;

        NPK_ASSERT(from.backing == BookBacking::Swap);
        NPK_ASSERT(to.backing == BookBacking::Swap);

        need = {};
        fill = nullptr;
        Private::TrieSpares nodes {};
        auto status = NpkStatus::Success;

        while (status == NpkStatus::Success && cursor < end)
        {
            auto section = RcuReadLock();

            size_t index;
            BookSlot* slot = BookTrie::Next(index, from.root, cursor);
            if (slot == nullptr || index >= end)
            {
                RcuReadUnlock(section);
                cursor = end;
                break;
            }

            const size_t stop = LeafStop(index, end);
            for (; index < stop; index++, slot++)
            {
                const BookEntry value = LockBookEntry(*slot, true);
                if (value == 0)
                    continue;

                if (BookFill* inFlight = BookEntryFill(value))
                {
                    inFlight->refs.FetchAdd(1, sl::Relaxed);
                    fill = inFlight;

                    UnlockBookEntry(*slot, value);
                    status = NpkStatus::Pending;
                    break;
                }

                //`to` is reached only through sharer lists, which never
                //walk its trie: plain stores fill it

                const size_t n = index - first;
                TopUpNodes(nodes, spares);
                BookSlot* dest = BookTrie::Ensure(to.root, n, nodes);
                AnonCell* cell = BookEntryCell(value);
                const bool needCell = cell == nullptr && spares.cellCount == 0;

                if (dest == nullptr || needCell)
                {
                    UnlockBookEntry(*slot, value);
                    status = NpkStatus::Shortage;
                    break;
                }

                if (cell != nullptr)
                {
                    const BookEntry inner = LockBookEntry(cell->word);

                    if (CellFull(cell) && spares.blockCount == 0)
                    {
                        UnlockBookEntry(cell->word, inner);
                        UnlockBookEntry(*slot, value);
                        status = NpkStatus::Shortage;
                        break;
                    }

                    if (CellFull(cell))
                        spares.blockCount--;

                    AddSharer(cell, { &to, n }, spares.blocks);
                    cell->refs.FetchAdd(1, sl::Relaxed);

                    UnlockBookEntry(cell->word, inner);
                    UnlockBookEntry(*slot, value);
                    dest->Store(MakeBookEntry(cell), sl::Release);

                    continue;
                }

                //create the cell with its lock bit held.
                cell = new(PopChain(spares.cells)) AnonCell {};

                spares.cellCount--;
                cell->refs.Store(2, sl::Relaxed);
                cell->inlineSharers[0] = { &from, index };
                cell->inlineSharers[1] = { &to, n };
                cell->sharerCount = 2;
                cell->word.Store(value | BookEntryLocked, sl::Relaxed);

                if (PageInfo* page = BookEntryPage(value); page != nullptr)
                    PageInfoSetVmOwner(page, cell, PageVmOwnerType::Cell);

                UnlockBookEntry(*slot, MakeBookEntry(cell));
                UnlockBookEntry(cell->word, value);
                dest->Store(MakeBookEntry(cell), sl::Release);
            }

            RcuReadUnlock(section);
            cursor = index;
        }

        ReturnNodes(nodes, spares);
        span.begin = static_cast<BookIndex>(cursor);
        if (status == NpkStatus::Shortage)
            CountPromoteNeed(need, span, first);

        return status;
    }

    NpkStatus BookFillPromoteSpares(BookPromoteSpares& spares,
        const BookPromoteNeed& need)
    {
        while (spares.cellCount < need.cells)
        {
            void* cell = PoolAllocWired(sizeof(AnonCell), BookCellTag);
            if (cell == nullptr)
                return NpkStatus::Shortage;

            NPK_ASSERT((reinterpret_cast<uintptr_t>(cell)
                & ~BookEntryAddrMask) == 0);

            PushChain(spares.cells, cell);
            spares.cellCount++;
        }

        while (spares.blockCount < need.blocks)
        {
            void* mem = PoolAllocWired(sizeof(AnonSharerBlock), BookCellTag);
            if (mem == nullptr)
                return NpkStatus::Shortage;

            auto* block = new(mem) AnonSharerBlock {};
            block->next = spares.blocks;
            spares.blocks = block;
            spares.blockCount++;
        }

        while (spares.nodeCount < need.nodes)
        {
            void* node = Private::AllocTrieNode(BookTrie::NodeSize);
            if (node == nullptr)
                return NpkStatus::Shortage;

            PushChain(spares.nodes, node);
            spares.nodeCount++;
        }

        return NpkStatus::Success;
    }

    void BookDrainPromoteSpares(BookPromoteSpares& spares)
    {
        while (void* cell = PopChain(spares.cells))
            PoolFreeWired(cell, sizeof(AnonCell), BookCellTag);

        while (spares.blocks != nullptr)
        {
            AnonSharerBlock* block = spares.blocks;
            spares.blocks = block->next;

            FreeSharerBlock(block);
        }

        while (void* node = PopChain(spares.nodes))
            Private::FreeTrieNode(node, BookTrie::NodeSize);

        spares = {};
    }

    bool BookCellBeginSweep(BookCellSnapshot& out, AnonCell* cell,
        PageInfo* page)
    {
        auto section = RcuReadLock();

        const BookEntry inner = LockBookEntry(cell->word, true);
        const bool holds = inner == MakeBookEntry(page);
        if (holds)
        {
            out.sharerGeneration = cell->sharerGeneration;
            out.commitGeneration = cell->commitGeneration;
            out.sharers = cell->sharerCount;
        }

        if (inner != 0)
            UnlockBookEntry(cell->word, inner);
        RcuReadUnlock(section);

        return holds;
    }

    bool BookCellSharer(AnonSharer& out, AnonCell* cell,
        const BookCellSnapshot& snap, size_t index, BookTryRefFunc hold)
    {
        auto section = RcuReadLock();

        const BookEntry inner = LockBookEntry(cell->word, true);
        bool found = inner != 0
            && cell->sharerGeneration == snap.sharerGeneration
            && index < cell->sharerCount;

        if (found)
        {
            out = SharerAt(cell, index);
            found = hold(out.book);
        }

        if (inner != 0)
            UnlockBookEntry(cell->word, inner);
        RcuReadUnlock(section);

        return found;
    }

    bool BookCellEndSweep(AnonCell* cell, PageInfo* page,
        const BookCellSnapshot& snap, bool dirtied, bool unmapped)
    {
        auto section = RcuReadLock();

        const BookEntry inner = LockBookEntry(cell->word, true);
        if (inner != MakeBookEntry(page))
        {
            if (inner != 0)
                UnlockBookEntry(cell->word, inner);
            RcuReadUnlock(section);

            return false;
        }

        SwapHandle dead = NoSwapHandle;
        if (dirtied)
            dead = MarkWrittenLocked(true, page);

        const bool still = cell->sharerGeneration == snap.sharerGeneration
            && cell->commitGeneration == snap.commitGeneration;
        if (unmapped && still)
            ClearPageMapped(page);

        const bool single = cell->sharerCount == 1;

        UnlockBookEntry(cell->word, inner);
        RcuReadUnlock(section);
        FreeHandle(dead);

        return single;
    }

    bool BookUnpromote(AnonCell* cell, PageInfo* page, BookTryRefFunc hold,
        BookUnrefFunc drop)
    {
        auto section = RcuReadLock();

        AnonSharer sole {};
        BookEntry inner = LockBookEntry(cell->word, true);

        bool held = inner == MakeBookEntry(page) && cell->sharerCount == 1;
        if (held)
        {
            sole = cell->inlineSharers[0];
            held = hold(sole.book);
        }

        if (inner != 0)
            UnlockBookEntry(cell->word, inner);
        RcuReadUnlock(section);
        if (!held)
            return false;

        Book& book = *sole.book;
        const auto locked = AcquireSxMutexExclusive(&book.mutex, {},
            NPK_WAIT_LOCATION);
        if (locked != NpkStatus::Success)
        {
            NPK_ASSERT(locked == NpkStatus::Timeout);
            drop(sole.book);

            return false;
        }

        const bool done = UnpromoteLocked(book, sole.offset, cell, page);
        ReleaseSxMutexExclusive(&book.mutex);

        if (done)
            BookUnrefCell(cell);
        drop(sole.book);

        return done;
    }

    bool BookTryUnpromote(Book& book, size_t offset, AnonCell* cell,
        PageInfo* page)
    {
        const auto locked = AcquireSxMutexExclusive(&book.mutex, {},
            NPK_WAIT_LOCATION);
        if (locked != NpkStatus::Success)
        {
            NPK_ASSERT(locked == NpkStatus::Timeout);

            return false;
        }

        const bool done = UnpromoteLocked(book, offset, cell, page);
        ReleaseSxMutexExclusive(&book.mutex);

        if (done)
            BookUnrefCell(cell);

        return done;
    }

    void BookSettleSweep(Book& book, PageInfo* page, bool dirtied,
        bool forget)
    {
        NPK_ASSERT(BookOwnerOf(page) == &book);

        auto section = RcuReadLock();
        BookSlot* slot = BookTrie::Lookup(book.root, page->offset);
        NPK_ASSERT(slot != nullptr);

        const BookEntry value = LockBookEntry(*slot, true);
        NPK_ASSERT(value == MakeBookEntry(page));

        SwapHandle dead = NoSwapHandle;
        if (dirtied)
            dead = MarkWrittenLocked(book.backing == BookBacking::Swap, page);
        if (forget)
            ClearPageMapped(page);

        UnlockBookEntry(*slot, value);
        RcuReadUnlock(section);
        FreeHandle(dead);
    }

    void BookRemoveRange(BookSpan span, BookPageSink sink, void* arg)
    {
        Book& book = *span.book;
        const size_t end = span.end;
        size_t cursor = span.begin;

        while (cursor < end)
        {
            RemovalBatch take {};
            AnonCell* cells[RemoveBatchSize];
            PageInfo* lonePages[RemoveBatchSize];
            AnonSharerBlock* blocks[RemoveBatchSize];
            size_t cellCount = 0;
            size_t blockCount = 0;

            auto section = RcuReadLock();

            size_t index;
            BookSlot* slot = BookTrie::Next(index, book.root, cursor);
            if (slot == nullptr || index >= end)
            {
                RcuReadUnlock(section);
                break;
            }

            size_t stop = LeafStop(index, end);
            if (index + RemoveBatchSize < stop)
                stop = index + RemoveBatchSize;

            for (; index < stop; index++, slot++)
            {
                const BookEntry value = LockBookEntry(*slot, true);
                if (value == 0)
                    continue;

                AnonCell* cell = BookEntryCell(value);
                if (cell == nullptr)
                {
                    TakeContent(take, value, index);
                    UnlockBookEntry(*slot, 0);
                    continue;
                }

                //the last sharer to leave takes what the cell holds
                const BookEntry inner = LockBookEntry(cell->word);
                AnonSharerBlock* empty = RemoveSharer(cell, &book, index);
                if (empty != nullptr)
                    blocks[blockCount++] = empty;

                //one sharer left in another book: worth an unpromote attempt
                BookEntry left = inner;
                PageInfo* lone = nullptr;
                if (cell->sharerCount == 0)
                {
                    TakeContent(take, inner, 0);
                    left = 0;
                }
                else if (cell->sharerCount == 1
                    && cell->inlineSharers[0].book != &book)
                {
                    lone = BookEntryPage(inner);
                }
                UnlockBookEntry(cell->word, left);
                UnlockBookEntry(*slot, 0);
                cells[cellCount] = cell;
                lonePages[cellCount] = lone;
                cellCount++;
            }
            RcuReadUnlock(section);

            BookTrie::Prune(book.root, stop - 1);

            for (size_t i = 0; i < blockCount; i++)
                FreeSharerBlock(blocks[i]);
            for (size_t i = 0; i < cellCount; i++)
            {
                //best effort: the trimmer's sweep is the backstop
                if (lonePages[i] != nullptr)
                {
                    (void)BookUnpromote(cells[i], lonePages[i],
                        AmapTryRefBook, AmapUnrefBook);
                }
                BookUnrefCell(cells[i]);
            }
            for (size_t i = 0; i < take.handleCount; i++)
                SwapFreeHandle(take.handles[i]);
            for (size_t i = 0; i < take.pageCount; i++)
                sink(take.pages[i], arg);

            cursor = stop;
        }
    }

    void BookDetachRange(Book& book, BookToRangeLink& link, BookPageSink sink,
        void* arg)
    {
        BookSpan rest = BookLinkSpan(book, link);

        auto result = AcquireSxMutexExclusive(&book.mutex, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        NPK_ASSERT(result == NpkStatus::Success);

        BookUnlinkRange(book, link);

        //asked again after each run: the next uncovered run starts past it
        BookSpan run;
        while (book.kind == BookKind::Amap && !book.ranges.Empty()
            && BookNextUncovered(run, rest))
        {
            BookRemoveRange(run, sink, arg);
            rest.begin = run.end;
        }

        ReleaseSxMutexExclusive(&book.mutex);
    }

    bool BookTakePage(PageInfo** out, Book& book, size_t offset,
        BookUntrack Untrack, BookRetrack Retrack)
    {
        *out = nullptr;

        auto result = AcquireSxMutexExclusive(&book.mutex, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        NPK_ASSERT(result == NpkStatus::Success);

        bool taken = true;
        SwapHandle dead = NoSwapHandle;
        while (true)
        {
            auto section = RcuReadLock();

            BookSlot* slot = BookTrie::Lookup(book.root, offset);
            BookEntry value = 0;
            if (slot != nullptr)
                value = LockBookEntry(*slot, true);

            PageInfo* page = BookEntryPage(value);
            if (value != 0)
                UnlockBookEntry(*slot, value);
            RcuReadUnlock(section);

            if (value == 0)
                break;

            if (page != nullptr && !Untrack(page))
            {
                taken = false;
                break;
            }

            section = RcuReadLock();
            const BookEntry now = LockBookEntry(*slot);

            if (page != nullptr)
            {
                NPK_ASSERT(now == MakeBookEntry(page));

                size_t ownPins = 0;
                if (PageHasFlag(page, PageVmFlag::Mapped))
                    ownPins = 1;

                taken = page->pins == ownPins;
                if (!taken)
                {
                    UnlockBookEntry(*slot, now);
                    RcuReadUnlock(section);
                    Retrack(page);
                    break;
                }

                ClearPageMapped(page);
                dead = page->swapHandle;
                page->swapHandle = NoSwapHandle;
                PageClearFlag(page, PageVmFlag::Clean);
                ClearPageOwner(page);

                UnlockBookEntry(*slot, 0);
                RcuReadUnlock(section);
                *out = page;
                break;
            }

            if (BookEntryPage(now) != nullptr)
            {
                //a lockless page-in published between the two sections
                UnlockBookEntry(*slot, now);
                RcuReadUnlock(section);
                continue;
            }

            NPK_ASSERT(GetBookEntryType(now) != BookEntryType::Cell);

            dead = BookEntrySwap(now);
            BookFill* fill = BookEntryFill(now);
            if (fill != nullptr)
                dead = fill->handles[offset - fill->first];

            NPK_ASSERT(now != 0);
            UnlockBookEntry(*slot, 0);
            RcuReadUnlock(section);

            break;
        }

        if (taken)
            BookTrie::Prune(book.root, offset);

        ReleaseSxMutexExclusive(&book.mutex);
        FreeHandle(dead);

        return taken;
    }

    void BookLinkRange(Book& book, BookToRangeLink& link)
    {
        book.ranges.PushBack(&link);
    }

    void BookUnlinkRange(Book& book, BookToRangeLink& link)
    {
        book.ranges.Remove(&link);
    }

    bool BookNextUncovered(BookSpan& out, BookSpan within)
    {
        Book& book = *within.book;
        const BookIndex end = within.end;
        BookIndex point = within.begin;
        bool moved = true;

        while (moved && point < end)
        {
            moved = false;
            for (auto it = book.ranges.Begin(); it != book.ranges.End(); ++it)
            {
                const BookSpan link = BookLinkSpan(book, *it);

                if (point >= link.begin && point < link.end)
                {
                    point = link.end;
                    moved = true;
                }
            }
        }

        if (point >= end)
            return false;

        BookIndex stop = end;
        for (auto it = book.ranges.Begin(); it != book.ranges.End(); ++it)
        {
            const BookSpan link = BookLinkSpan(book, *it);

            if (link.end != link.begin && link.begin > point
                && link.begin < stop)
            {
                stop = link.begin;
            }
        }

        out = { &book, point, stop };

        return true;
    }

    bool BookOverlapped(BookSpan span, const BookToRangeLink* except)
    {
        Book& book = *span.book;
        for (auto it = book.ranges.Begin(); it != book.ranges.End(); ++it)
        {
            if (&*it == except)
                continue;

            const BookSpan link = BookLinkSpan(book, *it);
            if (link.begin < span.end && span.begin < link.end)
                return true;
        }

        return false;
    }
}
