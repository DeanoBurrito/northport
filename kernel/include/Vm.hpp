#pragma once

#include <Core.hpp>
#include <lib/SeqCount.hpp>

namespace Npk
{
#define NPK_MAKE_HEAP_TAG(str) \
    ( (static_cast<HeapTag>(str[3]) << 24) \
    | (static_cast<HeapTag>(str[2]) << 16) \
    | (static_cast<HeapTag>(str[1]) << 8) \
    | (static_cast<HeapTag>(str[0]) ) \
    )

    /* A batch needs slack space in case the removal also frees intermediate
     * structures (like page tables), which may also become a covered region.
     */
    constexpr size_t MapBatchTableSlack = 8;

    enum class VmFlag
    {
        /* Set if memory is writable.
         */
        Write,

        /* Set if memory is executable.
         */
        Fetch,

        /* Set for mappings contain memory-mapped device registers, or memory
         * ranges that should be treated as such. This has implications for
         * caching and access rules.
         */
        Mmio,

        /* Indicates any writes to a block of memory are private to the address
         * space, and therefore not propagated back to the original source.
         */
        CopyOnWrite,

        /* Internal flag: indicates if a range is borrowing the amap of another.
         */
        AmapNeedsCopy,
    };

    using VmFlags = sl::Flags<VmFlag>;

    using SwapSlot = uint64_t;

    /* Describes how entries are added and removed from the book, and what an
     * empty entry should mean.
     */
    enum class BookBacking : uint8_t
    {
        /* Backed by swap, empty entries read as zeroes until dirtied once.
         */
        Swap,

        /* All entries are filled and drained via a pager.
         */
        Pager,
    };

    /* Describes which parent struct the book is embedded in.
     */
    enum class BookKind : uint8_t
    {
        Amap,
        Source,
    };

    /* Forward declarations.
     */
    struct VmSpace;
    struct VmSource;
    struct Iop;
    struct IoInterface;
    struct NsObject;
    struct VmRange;
    struct AnonCell;

    /* Linkage for a VmRange's association with a book struct. Forms the
     * book <-> mapping half of the pv-map. The other half (book <-> page) is
     * formed by the `PageInfo::owner`.
     */
    struct BookToRangeLink
    {
        sl::ListHook hook;
        VmRange* range;
    };

    using BookToRangeList = sl::List<BookToRangeLink, &BookToRangeLink::hook>;

    /* Feedback and control data for readahead on a book instance. This tracks
     * how well readahead has worked for the instance, it's a bit racy but
     * it's also a best-effort over a best-effort.
     */
    struct BookReadaheadFeedback
    {
        sl::Atomic<uint32_t> issued; //pages published by readahead.
        sl::Atomic<uint32_t> used; //published pages actually used.
        sl::Atomic<uint32_t> lastSwapPlusOne; //adjacency tracking, see linux.
        sl::Atomic<uint16_t> swapHits; //swapped-in pages used since last swapin
        sl::Atomic<uint16_t> swapWindow; //readahead window size
    };

    /* A sparse collection of pages, addressable by their offset into the book.
     * The "Book" name is copied from MINTIA2 discussions/sources (thanks!).
     *
     * Book contents should only be accessed via helper functions. For posterity
     * the protocol is as follows:
     * - An exclusive owner of the mutex can modify the trie's shape, change
     *   linked ranges, transition an entry to empty, or sweep an unshared
     *   page's mappings.
     * - A shared holder of the mutex can fill an empty entry, or commit a
     *   mapping.
     * - An RCU reader can keep the book alive, and the trie can be navigated in
     *   a reader's critical section via per-entry valid/vacant bit.
     */
    struct Book
    {
        /* Protects freeing of this book's memory, see protocol above.
         */
        EbrItem retire;

        /* Protects internal state, see access protocol above.
         */
        SxMutex mutex;

        /* Root address of the trie of cells, where a cell is a tagged pointer
         * linking directly or indirectly (for shared pages) to the PageInfo
         * at an offset.
         */
        sl::Atomic<uintptr_t> root;

        /* List of mappings of this book.
         */
        BookToRangeList ranges;

        /* Count of in-progress unmaps of pages from this book, typically they
         * are waiting on an invalidation to complete. While non-zero this
         * field prevents the `Mapped` flag of this book's pages being cleared.
         */
        sl::Atomic<size_t> removalsInFlight;

        /* Readahead effectiveness tracking, answers the question of "worth it?"
         * at runtime (per book).
         */
        BookReadaheadFeedback readahead;

        /* How pages are moved in-to and out-of this book.
         */
        BookBacking backing;

        /* The struct this book is embedded into.
         */
        BookKind kind;
    };
    static_assert(offsetof(Book, retire) == 0);

    /* Represents a set of pages over a VmRange, can be CoW'd.
     */
    struct Amap
    {
        Book book;
        sl::RefCount refcount;
    };
    static_assert(offsetof(Amap, book) == 0);

    namespace Private
    {
        void DestroyAmap(Amap* map);
    }

    using AmapRef = sl::Ref<Amap, &Amap::refcount, Private::DestroyAmap>;

    enum class VmAdvice : uint8_t
    {
        Normal = 0,
        Random,
        Sequential,
        WillNeed,
    };

    struct VmRange
    {
        /* Ranges are detached from a space via RCU, allowing lookup to be
         * lockless (8D).
         */
        EbrItem retire;

        /* Linkage to the upper layer's book (amap).
         */
        BookToRangeLink upperLink;

        /* Linkage to the lower layer's book (source).
         */
        BookToRangeLink lowerLink;

        /* Synchronizes access to all fields below.
         */
        Mutex mutex;

        /* Count of faults looking at this range, part of the lockless lookup
         * protocol.
         */
        sl::Atomic<uint32_t> faultRefs;

        /* Faults drop the mutex while waiting for the backing page to become
         * ready, this field is incremented any time state that would affect a
         * fault is changed. This can save a good chunk of work when the fault
         * re-acquires the lock and attempts to map the page.
         */
        uint32_t version;

        /*
         */
        Condition drained;

        /* Linkage for VmSpace management.
         */
        sl::RBTreeHook spaceHook;

        /* Address space this range describes.
         */
        VmSpace* space;

        /* Flags describing the behaviour of this range.
         */
        VmFlags flags;

        /* Helps inform fault handler how the range is being accessed, can allow
         * a range to be treated as if it has `Sequential` advice even if it
         * doesn't.
         */
        uint32_t nextFaultSlotPlusOne;

        /* How this range is likely to be accessed, set explicitly.
         */
        VmAdvice advice;

        /* Helps inform clustered pagein: the cluster size the last fault chose,
         * before any memory pressure cuts.
         */
        uint8_t readaheadWindow;

        /* Base address of this address range, relative to the parent
         * address space. This can also be thought of as an offset,
         * but that field name is used below for a different meaning.
         */
        size_t base;

        /* Length of the address range. May be read without holding `mutex`.
         */
        sl::Atomic<size_t> length;

        /* Owning reference to this range's anon map structure.
         */
        AmapRef amapRef;

        /* Offset of this range within its referenced amap, allows efficient
         * splitting of a range as the new ranges can continue to refer to the
         * old amap.
         */
        size_t amapOffset;

        /* Source object (layer 2) providing physical pages for this range
         * if not found in layer 1 (the amap). If `source` is null, this range
         * is a zero-fill area.
         */
        VmSource* source;

        /* Offset within the source object that this range begins at.
         */
        size_t offset;
    };
    static_assert(offsetof(VmRange, retire) == 0);

    struct VmRangeLt
    {
        bool operator()(const VmRange& a, const VmRange& b)
        {
            return a.base < b.base;
        }
    };

    using VmRangeTree = sl::RBTree<VmRange, &VmRange::spaceHook, VmRangeLt>;

    struct VmFreeRange
    {
        sl::RBTreeHook hook;
        size_t base;
        size_t length;
        size_t largestChild;
    };

    struct VmFreeRangeLt
    {
        bool operator()(const VmFreeRange& a, const VmFreeRange& b)
        {
            return a.base < b.base;
        }
    };

    struct VmFreeRangeAggregator
    {
        static bool Aggregate(VmFreeRange* range);
    };

    using VmFreeRangeTree = sl::RBTree<VmFreeRange, &VmFreeRange::hook, 
        VmFreeRangeLt, VmFreeRangeAggregator>;

    struct VmSpace
    {
        HwMap* map;

        Mutex freeRangesMutex;
        VmFreeRangeTree freeRanges;

        SxMutex rangesMutex;
        VmRangeTree ranges;
    };

    enum class PagerFlag
    {
        Write,
        Locked,
        AllPages,
    };

    using PagerFlags = sl::Flags<PagerFlag>;

    /* Used to move metadata for a page to/from pagers, as pagers will need to
     * store and load metadata from the backing store but cannot activate the
     * metadata (they might not have the capabilities to do so).
     * The buffer (`bytes`) is zeroed before being passed to a pager's Get().
     *
     * If a target doesnt have any metadata, pointers to this struct can be left
     * null.
     */
    struct VmPagerMeta
    {
        uint8_t* bytes;
        size_t bytesPerPage;
    };

    /* Functions for moving data between backing store and VmSource's book.
     *
     * For functions taking a page info span, the caller allocates these but is
     * allowed to leave null entries mid-span to indicate it doesnt care about
     * that page. The offset applies to `pages[0]` in the source, and
     * `mainIndex` is relative to the offset, and indicates the page the caller
     * really cares about. The other pages are nice-to-haves but not needed now.
     * The return status refers to what happened regarding the main page.
     *
     * On Get()/Fault() ops the caller will clear the `Filled` flag on each
     * non-null page. If the pager filled the page, it must re-set that flag.
     */
    struct VmPagerOps
    {
        /* Attempts to acquire a reference to the source, returns success.
         *
         * Implementations are not allowed to block and should generally keep
         * this short, callers are already under several layers of locks.
         * Spinlocks should only be taken internally with great care that they
         * can never lead back to any state shared with the trimmer or fault
         * handler. It's preferable to not take any locks at all if possible.
         */
        bool (*RefObj)(VmSource* src, PagerFlags flags);

        /* Undoes a previously acquired reference from `RefObj()`. See notes on
         * that function.
         */
        void (*UnrefObj)(VmSource* src);

        /* Fills `pages` with data, and the caller guarantees pages are zero
         * filled. May block and/or take locks.
         *
         * May be nullptr for pagers that dont support returning page data,
         * in that case they must provide an implementation for `Fault()`.
         */
        NpkStatus (*Get)(VmSource* src, sl::Span<PageInfo*> pages,
            size_t pagesOffset, size_t mainIndex, VmPagerMeta* meta,
            PagerFlags flags);

        /* Like `Get()` but establishes the mappings itself and returns after
         * they're added. Suitable for memory the VMM can't manage, like MMIO.
         *
         * This function is only called for pagers that dont provide a valid
         * `Get()` routine. Only one of these functions will be called for a
         * pager.
         *
         * The `vaddr` arg is the virtual address of `pages[0]`, `pages` acts as
         * a bitmap of which pages are wanted, but the page info structs
         * themselves are not valid and should not be accessed.
         */
        NpkStatus (*Fault)(VmSource* src, VmSpace& space, uintptr_t vaddr,
            sl::Span<PageInfo*> pages, size_t pagesOffset, size_t mainIndex,
            PagerFlags flags);

        /* For pages provided by `Get()` this function performs the opposite
         * function: data is copied from `pages` to the backing store.
         * There is no main index here for obvious reasons.
         */
        NpkStatus (*Put)(VmSource* src, sl::Span<PageInfo*> pages,
            size_t pagesOffset, const VmPagerMeta* meta, PagerFlags flags);

        void (*Flush)(VmSource* src, size_t offsetPages, size_t lengthPages,
            PagerFlags flags);
        void (*Release)(VmSource* src, PageInfo* page, size_t pageOffset);
    };

    struct VmSource
    {
        /* Book struct for managing the pages for this source.
         */
        Book book;

        /* Set before the source is known to the VM subsystem. Readonly after.
         */
        const VmPagerOps* ops;

        /* Seqlock for reading sizes of this source.
         */
        sl::SeqCount sizeSeq;

        /* Synchronizes writers to the source's sizes.
         */
        IplSpinLock<Ipl::Dpc> sizeLock;

        /* Whether this source has a defined size.
         */
        bool bounded;

        /* Authoritative copy of the limit of this source, providers must keep
         * this up to date with their internal data. This value indicates the
         * absolute limit to what the source can or will hold.
         */
        uint64_t size;

        /* The limit of what the source's pager can provide bytes for,
         * anything past this length is populated by the VM and fed back to the
         * pager. Accesses beyond `initializedSize` are allowed and will be zero
         * filled.
         */
        uint64_t initializedSize;
    };
    SL_ASSERT_OFFSET(VmSource, book, 0);

    /* Snapshot of a source's size state, fields can be used directly.
     */
    struct VmSourceSize
    {
        uint64_t size;
        uint64_t initializedSize;
        bool bounded;
    };

    /* Provides fine control over address space allocation. Each field has a
     * sane default value and can be left untouched if the caller doesn't care
     * to select values.
     */
    struct AllocConstraints
    {
        /* If non-zero, requests the a specific address for the allocation.
         * If `hardPreference` is set allocation will fail if the preferred
         * address is unavailable, otherwise the allocator will choose a nearby
         * address. The `topDown` field indicates whether the nearby address
         * should be above (`topDown=false`) or below (`topDown=true`).
         * Note that this address can be outside the range specified by
         * `minAddr` and `maxAddr`.
         */
        uintptr_t preferredAddr = 0;

        /* Lowest address considered for allocation.
         */
        uintptr_t minAddr = 0;

        /* Highest address considered for allocation.
         */
        uintptr_t maxAddr = static_cast<decltype(maxAddr)>(-1);

        /* Minimum alignment for allocated address, can be zero for
         * 'dont care'.
         */
        size_t alignment = 0;

        /* Affects interpretation of `preferredAddr`, see that field for
         * specifics.
         */
        bool hardPreference = false;

        /* If set the allocator will search for free address from the top
         * of the address space, otherwise it will be begin searching from
         * lower addresses.
         * This field also interacts with `preferredAddr`, see that field's
         * description for specifics.
         */
        bool topDown = false;

        /* Timeout used to acquire the allocator mutex.
         */
        sl::TimeCount timeout = sl::NoTimeout;
    };

    struct StackCacheStats
    {
        size_t cached;
        size_t doomed;
        size_t reused;
        size_t stolen;
        size_t allocated;
        size_t released;
        size_t live;
        size_t peak;
        size_t limit;
        size_t hardLimit;
        size_t deepest;
    };

    struct MapBatchRange
    {
        uintptr_t base;
        size_t length;
    };

    /* Collects ranges requiring invalidation, used to allow related but
     * sequential mmu ops to batch shootdowns.
     */
    struct MapBatch
    {
        HwMap* map;
        sl::Span<MapBatchRange> storage;
        PageList freed;
        size_t count;
        bool coarse;

        //safety net, catches a batch that wasn't finished or dropped.
        ~MapBatch();
    };

    /* Resets and initializes `batch` using caller-provided storage, and binds
     * it to `map`. The batch may be already bound to another map, but it must
     * not have any outstanding pages or invalidations (i.e. it must have been
     * passed to `HwMapUpdate()` recently).
     */
    void ResetMapBatch(MapBatch& batch, HwMap* map,
        sl::Span<MapBatchRange> store);

    /* Indicates that a caller is done with a batch. For correctness the batch
     * must be empty before passing it to this function.
     */
    void FinishMapBatch(MapBatch& batch);

    /* Empties a batch without invalidating any of its ranges, essentially
     * dropping them. Useful if the batch's owner decides the invalidation can
     * be avoided or is not worth it.
     * Queued pages for freeing are *not* dropped and therefore this batch still
     * needs to be passed to `HwMapUpdate()` at some point.
     */
    void MapBatchDropRanges(MapBatch& batch);

    /* Initializes the kernel's virtual memory space, which makes all virtual
     * memory services available (kernel pool, file cache).
     * The `base` and `len` params describe two regions of usable address space
     * for VM services. There is no assumptions made about the two regions,
     * but they after typically used to describe the space before and after
     * the kernel image in memory.
     */
    void InitKernelVmSpace(uintptr_t lowBase, size_t lowLen, uintptr_t highBase,
        size_t highLen);

    /* Sugar function for adding a mapping to the kernel map.
     */
    NpkStatus SetKernelMap(uintptr_t vaddr, Paddr paddr, VmFlags flags);

    /* Sugar function for removing a mapping from the kernel map, optionally
     * places the translated-at physical address into `paddr` if non-null.
     */
    NpkStatus ClearKernelMap(MapBatch& batch, Paddr* paddr, uintptr_t vaddr);

    /* Attempts to allocate a kernel stack. The architectural base is placed
     * in `*stack` on success, this is the value that is typically placed into
     * the general purpose stack pointer register.
     * The stack is sized as `KernelStackSize()` and is zeroed.
     *
     * Must be called at passive IPL.
     */
    NpkStatus AllocKernelStack(void** stack);

    /* Marks a kernel stack (as returned from `AllocKernelStack()`) as no longer
     * in use.
     *
     * Freed stacks go through a cache layer so this function doesn't block
     * or touch the backing allocator itself directly, safe to call at or below
     * DPC IPL.
     */
    void FreeKernelStack(void* stack);

    /* Populates a stat block with global stats of the kernel stack cache
     * layer. Note these stats are correct at the time of being written but
     * nocessarily cohesive with each other. These numbers are advisory, their
     * exactly value shouldn't be depended on.
     */
    void GetStackCacheStats(StackCacheStats& out);

    /* Attempts to allocate `len` bytes from a general purpose pool, with the
     * specified allocation tag. Returns `nullptr` on failure. The `wired` param
     * determines if the memory is unavailable to be paged out. Non wired memory
     * should be preferred where possible but it can only be accessed at passive
     * IPL.
     *
     * This function must be called from passive IPL.
     */
    void* PoolAlloc(size_t len, HeapTag tag, bool wired, sl::TimeCount timeout 
        = sl::NoTimeout);

    /* Attempts to free `len` bytes `ptr` from either the paged or wired pools,
     * as specified by `wired`. This function can fail for a number of reasons,
     * including if `tag` does not match what was passed to the alloc call that
     * returned this pointer.
     * Must be called from passive IPL.
     */
    NpkStatus PoolFree(void* ptr, size_t len, HeapTag tag, bool wired, 
        sl::TimeCount timeout = sl::NoTimeout);

    /* Attempts to allocate `len` bytes from the paged pool, with the specified
     * allocation tag. Returns `nullptr` on failure.
     *
     * Paged allocations are only safe to access from passive IPL. This function
     * must be called from passive IPL.
     */
    SL_ALWAYS_INLINE
    void* PoolAllocPaged(size_t len, HeapTag tag, sl::TimeCount timeout 
        = sl::NoTimeout)
    {
        return PoolAlloc(len, tag, false, timeout);
    }

    /* Attempts to allocate `len` bytes from the wired pool, with the specified
     * allocation tag. Returns `nullptr` on failure.
     * This function must be called from passive IPL.
     */
    SL_ALWAYS_INLINE
    void* PoolAllocWired(size_t len, HeapTag tag, sl::TimeCount timeout 
        = sl::NoTimeout)
    {
        return PoolAlloc(len, tag, true, timeout);
    }

    /* Attempts to free `len` bytes at `ptr`. Partial frees of an allocated
     * region are not permitted, the length is used as a hint for the free
     * routine but is required to be the same as the one passed to the matching
     * PoolAllocPaged() call. The allocator tag must also match.
     * Returns whether freeing was successfully or not.
     */
    SL_ALWAYS_INLINE
    NpkStatus PoolFreePaged(void* ptr, size_t len, HeapTag tag, 
        sl::TimeCount timeout = sl::NoTimeout)
    {
        return PoolFree(ptr, len, tag, false, timeout);
    }

    /* Attempts to free `len` bytes at `ptr`. Partial frees of an allocated
     * region are not permitted, the length is used as a hint for the free
     * routine but is required to be the same as the one passed to the matching
     * PoolAllocPaged() call. The allocator tag must also match.
     * Returns whether freeing was successfully or not.
     */
    SL_ALWAYS_INLINE
    NpkStatus PoolFreeWired(void* ptr, size_t len, HeapTag tag, 
        sl::TimeCount timeout = sl::NoTimeout)
    {
        return PoolFree(ptr, len, tag, true, timeout);
    }

    /* Pins the page at `source:index`. This function may block if the page
     * needs to be fetched from backing storage. Pinned page is placed in
     * `*out` on success.
     */
    NpkStatus SourcePagePin(PageInfo** out, VmSource* source, size_t index);

    /* Unpins `page` that was previously pinned and returned from
     * `SourcePagePin()`.
     */
    void SourcePageUnpin(VmSource* source, PageInfo* page);

    /* Returns whether a `source:index` is currently resident.
     */
    bool SourcePageResident(VmSource* source, size_t index);

    /* Indicates the page at `source:index` is about to have its data changed
     * via a path outside of the source. The resident page is dropped, in flight
     * fills are failed.
     * May return `Busy` if an unstoppable op is using the source right now.
     * The operation can be retried.
     */
    NpkStatus SourcePageDrop(VmSource* source, size_t index);

    /* Returns a snapshot of a source's extents. Can be called at or below DPC
     * IPL.
     */
    VmSourceSize GetSourceSize(const VmSource* source);

    /* Enables or disables the bounds of a source, depending on `bounded`.
     * If set the source uses `size` and `initializedSize` for its own values,
     * see their definitions for how they interact.
     * If clear the source becomes unbounded (the default) and the size fields
     * have no effect.
     * Must be called at passive IPL. Callers of this function (the page
     * provider) must serialize calls to this function.
     */
    NpkStatus ResetSourceSize(VmSource* source, uint64_t size,
        uint64_t initializedSize, bool bounded);

    /* Sets the content size of `source`. Shrinking also clips
     * `initializedSize` and discards pages past the new bounds (partial pages
     * beyond bounds are zeroed).
     * May return `Busy` if an unstoppable interaction is in flight, callers
     * should retry with the same size until successful.
     *
     * NOTE: for anon sources private CoW'd pages in place are left alone,
     * the code necessary to remove them isn't worth it imo. XNU/UVM also do
     * this so it's not breaking any posix spec.
     */
    NpkStatus SetSourceSize(VmSource* source, uint64_t size);

    /* Advances the initialized size for a source, cannot decrement via this
     * function. Should be called once the backing bytes for a source include
     * up to `initializedSize` and can be relied upon to exist.
     */
    NpkStatus SetSourceInitializedSize(VmSource* source,
        uint64_t initializedSize);

    /* Attempts to allocate a range of `length` bytes in an address space. If
     * successful the allocated address is placed in `*addr`, otherwise `*addr`
     * is left unchanged.
     * The `constraints` argument allows for fine-tuning how the address space
     * should be selected, see the struct definition for details.
     */
    NpkStatus SpaceAlloc(VmSpace& space, uintptr_t* addr, size_t length, 
        AllocConstraints constraints = {});

    /* Releases `length` bytes of address space from `base` in address space
     * `space`, making them available for future allocations. Note this
     * address space may not be immediately available for reuse. This function
     * does not take care of unmapping anything in this space.
     */
    NpkStatus SpaceFree(VmSpace& space, uintptr_t base, size_t length, 
        sl::TimeCount timeout = sl::NoTimeout);

    /* TODO:
     * - if `base` is not page-aligned, this function should allocate the base
     *   address.
     */
    NpkStatus SpaceAttach(VmRange** range, VmSpace& space, uintptr_t base, 
        size_t length, VmSource* source, size_t srcOffset, VmFlags flags);

    /* TODO:
     */
    NpkStatus SpaceDetach(VmSpace& space, VmRange* range, bool freeAddresses);

    /* TODO:
     */
    NpkStatus SpaceSplit(VmSpace& space, VmRange& range, size_t offset);

    /* TODO:
     */
    NpkStatus SpaceLookupLocked(VmRange** found, VmSpace& space, uintptr_t addr,
        size_t length);

    /* TODO:
     */
    NpkStatus SpaceLookup(VmRange** found, VmSpace& space, 
        uintptr_t addr);

    /* TODO:
     */
    NpkStatus SpaceClone(VmSpace** clone, VmSpace& source);

    struct CpuBitsetAlloc
    {
        static void* Allocate(size_t bytes)
        {
            return PoolAllocWired(bytes, NPK_MAKE_HEAP_TAG("BitS"));
        }

        static void Free(void* ptr, size_t bytes)
        {
            PoolFreeWired(ptr, bytes, NPK_MAKE_HEAP_TAG("BitS"));
        }
    };
}
