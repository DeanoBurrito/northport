#pragma once

#include <Core.hpp>

namespace Npk
{
#define NPK_MAKE_HEAP_TAG(str) \
    ( (static_cast<HeapTag>(str[3]) << 24) \
    | (static_cast<HeapTag>(str[2]) << 16) \
    | (static_cast<HeapTag>(str[1]) << 8) \
    | (static_cast<HeapTag>(str[0]) ) \
    )

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

    /* Forward declaration, see below.
     */
    struct VmSpace;

    /* Forward declaration, see below.
     */
    struct VmSource;

    struct AnonPage
    {
        sl::SpinLock lock;
        sl::RefCount refcount;
        PageInfo* page;
        SwapSlot swapSlot;
    };

    namespace Private
    {
        void DestroyAnonPage(AnonPage* page);
    }

    using AnonPageRef = sl::Ref<AnonPage, &AnonPage::refcount,
        Private::DestroyAnonPage>;

    struct VmRangeAmapLink
    {
        sl::ListHook hook;
    };

    using AmapVmRangeList = sl::List<VmRangeAmapLink, &VmRangeAmapLink::hook>;

    struct AnonMap
    {
        sl::RefCount refcount;
        Mutex mutex;
        size_t slotCount;
        void* slots;
        AmapVmRangeList ranges;
    };

    namespace Private
    {
        void DestroyAnonMap(AnonMap* map);
    }

    using AnonMapRef = sl::Ref<AnonMap, &AnonMap::refcount,
        Private::DestroyAnonMap>;

    enum class VmAdvice : uint8_t
    {
        Normal = 0,
        Random,
        Sequential,
        WillNeed,
    };

    struct VmRange
    {
        VmRangeAmapLink amapLink;

        Mutex mutex;

        /* Linkage for VmSpace management.
         */
        sl::RBTreeHook spaceHook;

        /* Linkage for the PV list of the source layer. Only valid when `source`
         * is non-null.
         */
        sl::ListHook sourceHook;

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
        uint32_t lastFaultSlotPlusOne;

        /* How this range is likely to be accessed, set explicitly.
         */
        VmAdvice advice;

        /* Base address of this address range, relative to the parent
         * address space. This can also be thought of as an offset,
         * but that field name is used below for a different meaning.
         */
        size_t base;

        /* Length of the address range.
         */
        size_t length;

        /* Owning reference to this range's anon map structure.
         */
        AnonMapRef amapRef;

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
    static_assert(offsetof(VmRange, amapLink) == 0);

    using VmRangeSourceList = sl::List<VmRange, &VmRange::sourceHook>;

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

    struct VmPagerOps
    {
        bool (*RefObj)(VmSource* src, PagerFlags flags);
        void (*UnrefObj)(VmSource* src);
        NpkStatus (*Get)(VmSource* src, sl::Span<PageInfo> pages, 
            size_t pagesOffset, size_t mainIndex, PagerFlags flags);
        NpkStatus (*Fault)(VmSource* src, VmSpace& space, uintptr_t vaddr, 
            sl::Span<PageInfo> pages, size_t pagesOffset, size_t mainIndex, 
            PagerFlags flags);
        NpkStatus (*Put)(VmSource* src, sl::Span<PageInfo> pages, 
            size_t pagesOffset, PagerFlags flags);
        void (*Flush)(VmSource* src, size_t offsetPages, size_t lengthPages, 
            PagerFlags flags);
        void (*Release)(VmSource* src, PageInfo* page, size_t pageOffset);
    };

    struct VmSource
    {
        const VmPagerOps* ops; //set before source is known to vm subsystem, readonly after - not protected by lock

        SxMutex mutex;
        sl::FwdList<PageInfo, &PageInfo::vmoList> pages; //current list of pages
        sl::List<VmRange, &VmRange::sourceHook> ranges;
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
    };

    /* Initializes the kernel's virtual memory space, which makes all virtual
     * memory services available (kernel pool, file cache).
     * The `base` and `len` params describe two regions of usable address space
     * for VM services. There is no assumptions made about the two regions,
     * but they after typically used to describe the space before and after
     * the kernel image in memory.
     */
    void InitKernelVmSpace(uintptr_t lowBase, size_t lowLen, uintptr_t highBase,
        size_t highLen);

    NpkStatus SetKernelMap(uintptr_t vaddr, Paddr paddr, VmFlags flags);
    NpkStatus ClearKernelMap(uintptr_t vaddr, Paddr* paddr);

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
