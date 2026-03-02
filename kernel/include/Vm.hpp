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

    enum class VmStatus
    {
        Success,
        Shortage,
        InvalidArg,
        BadVaddr,
        AlreadyMapped,
        InUse,
        AlreadyAllocated,
        InternalError,
        ObjRefFailed,
    };

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
    };

    using VmFlags = sl::Flags<VmFlag>;

    /* A memory descriptor list (borrowed term) is a primitive for managing
     * buffers in virtual memory as a list of pages. An MDL references a number
     * of discontiguous physical pages that may (not) be backing a region of 
     * virtual memory.
     * Physical pages referenced by an MDL are wired (cannot be paged out 
     * or moved) and will have any pending copy-on-write actions resolved. 
     * Because of these semantics, MDLs should be used with care.
     */
    struct Mdl
    {
        /* Length of referenced memory in bytes.
         */
        size_t length;

        /* Offset of referenced memory into the first page (pages[0]), in bytes.
         */
        size_t offset;

        /* If the MDL is mapped in the kernel address space, `window` points
         * to the location. This field is validated and populated by VM
         * subsystem functions depending on need and should not be accessed
         * without a prior successful call to `MdlAcquireWindow()` on this MDL.
         */
        void* window;

        /* Pointers to the backing pages for the buffer. This array is always
         * `AlignUpPage(length) >> PageShift()` entries long.
         */
        PageInfo* pages[];
    };

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
        void* swapSlot;
    };

    namespace Private
    {
        void DestroyAnonPage(AnonPage* page);
    }

    using AnonPageRef = sl::Ref<AnonPage, &AnonPage::refcount,
        Private::DestroyAnonPage>;

    struct AnonMap
    {
        sl::RefCount refcount;
        Mutex mutex;
        size_t slotCount;
        void* slots;
    };

    namespace Private
    {
        void DestroyAnonMap(AnonMap* map);
    }

    using AnonMapRef = sl::Ref<AnonMap, &AnonMap::refcount,
        Private::DestroyAnonMap>;

    struct VmRange
    {
        /* Linkage for VmSpace management.
         */
        sl::RBTreeHook spaceHook;

        /* Flags describing the behaviour of this range.
         */
        VmFlags flags;

        /* Base address of this address range, relative to the parent
         * address space. This can also be thought of as an offset,
         * but that field name is used below for a different meaning.
         */
        size_t base;

        /* Length of the address range.
         */
        size_t length;

        AnonMapRef amapRef;
        size_t amapOffset;

        /* Source object (layer 2) providing physical pages for this range
         * if not found in layer 1 (the amap). If `source` is null, this range
         * is zero-fill area.
         */
        VmSource* source;

        /* Offset within the source object that this range begins at.
         */
        size_t offset;
    };

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

    enum class PagerStatus
    {
    };

    struct VmPagerOps
    {
        bool (*RefObj)(VmSource* src, PagerFlags flags);
        void (*UnrefObj)(VmSource* src);
        PagerStatus (*Get)(VmSource* src, sl::Span<PageInfo> pages, 
            size_t pagesOffset, size_t mainIndex, PagerFlags flags);
        PagerStatus (*Fault)(VmSource* src, sl::Span<PageInfo> pages, 
            size_t pagesOffset, size_t mainIndex, PagerFlags flags);
        PagerStatus (*Put)(VmSource* src, sl::Span<PageInfo> pages, 
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

    /* Initializes the kernel's virtual memory space, which makes all virtual
     * memory services available (kernel pool, file cache).
     * The `base` and `len` params describe two regions of usable address space
     * for VM services. There is no assumptions made about the two regions,
     * but they after typically used to describe the space before and after
     * the kernel image in memory.
     */
    void InitKernelVmSpace(uintptr_t lowBase, size_t lowLen, uintptr_t highBase,
        size_t highLen);

    /* Allocates and emplaces all intermediate page tables required for mapping
     * a physical address at `vaddr` using `map`, but does not populate the last
     * PTE. This allows later code to complete the mapping without requiring
     * access to allocators or other resources, by only manipulating the bits
     * of the final PTE.
     */
    VmStatus PrimeMapping(HwMap map, uintptr_t vaddr, MmuWalkResult& result, 
        PageAccessRef& ref);

    /* Sets the mapping for kernel translations at `vaddr` using `map` (direct
     * or indirect translations) to `paddr`. A direct translation one performed
     * by the hardware while the mapped is loaded and active, an indirect 
     * translation is performed by software. 
     * This is a low level function and skips some checks and features provided
     * at high levels in the virtual memory subsystem.
     */
    VmStatus SetMap(HwMap map, uintptr_t vaddr, Paddr paddr, VmFlags flags);

    /* Ensures that address translations for `vaddr` using `map` (directly or
     * indirectly) will fail, and returns whether a mapping was undone
     * (something was mapped prior to this call).
     * If this function returns success and `paddr` is non-null, the unmapped
     * physical address is placed in `*paddr`.
     */
    VmStatus ClearMap(HwMap map, uintptr_t vaddr, Paddr* paddr);

    /* Same as `PrimeMap()` but exclusively uses the current kernel map.
     */
    VmStatus PrimeKernelMap(uintptr_t vaddr);

    /* Same as `SetMap()` but exclusively uses the current kernel map.
     */
    VmStatus SetKernelMap(uintptr_t vaddr, Paddr paddr, VmFlags flags);

    /* Same as `ClearMap()` but exclusively uses the current kernel map.
     */
    VmStatus ClearKernelMap(uintptr_t vaddr, Paddr* paddr);

    /* Attempts to allocate a kernel stack, the base (higher numerical) address
     * is placed into `*stack` on success. This memory is backed immediately.
     */
    VmStatus AllocKernelStack(void** stack);

    /* Immediately released memory used by a kernel stack, DO NOT call this
     * for the current stack (insert stick in bicycle spoke meme here) - instead
     * a defer-based mechanism should be used (RCU, DPCs, WorkItems).
     */
    void FreeKernelStack(void* stack);

    void* PoolAlloc(size_t len, HeapTag tag, bool wired, sl::TimeCount timeout 
        = sl::NoTimeout);
    bool PoolFree(void* ptr, size_t len, HeapTag tag, bool wired, 
        sl::TimeCount timeout = sl::NoTimeout);

    SL_ALWAYS_INLINE
    void* PoolAllocPaged(size_t len, HeapTag tag, sl::TimeCount timeout 
        = sl::NoTimeout)
    {
        return PoolAlloc(len, tag, false, timeout);
    }

    SL_ALWAYS_INLINE
    void* PoolAllocWired(size_t len, HeapTag tag, sl::TimeCount timeout 
        = sl::NoTimeout)
    {
        return PoolAlloc(len, tag, true, timeout);
    }

    SL_ALWAYS_INLINE
    bool PoolFreePaged(void* ptr, size_t len, HeapTag tag, sl::TimeCount timeout
        = sl::NoTimeout)
    {
        return PoolFree(ptr, len, tag, false, timeout);
    }

    SL_ALWAYS_INLINE
    bool PoolFreeWired(void* ptr, size_t len, HeapTag tag, sl::TimeCount timeout
        = sl::NoTimeout)
    {
        return PoolFree(ptr, len, tag, true, timeout);
    }

    /* Attempts to allocate a range of `length` bytes in an address space. If
     * successful the allocated address is placed in `*addr`, otherwise `*addr`
     * is left unchanged.
     * The `constraints` argument allows for fine-tuning how the address space
     * should be selected, see the struct definition for details.
     */
    VmStatus SpaceAlloc(VmSpace& space, uintptr_t* addr, size_t length, 
        AllocConstraints constraints = {});

    /* Releases `length` bytes of address space from `base` in address space
     * `space`, making them available for future allocations. Note this
     * address space may not be immediately available for reuse. This function
     * does not take care of unmapping anything in this space.
     */
    VmStatus SpaceFree(VmSpace& space, uintptr_t base, size_t length, 
        sl::TimeCount timeout = sl::NoTimeout);

    /* TODO:
     * - if `base` is not page-aligned, this function should allocate the base
     *   address.
     */
    VmStatus SpaceAttach(VmRange** range, VmSpace& space, uintptr_t base, 
        size_t length, VmSource* source, size_t srcOffset, VmFlags flags);

    /* TODO:
     */
    VmStatus SpaceDetach(VmSpace& space, VmRange& range, bool freeAddresses);

    /* TODO:
     */
    VmStatus SpaceSplit(VmSpace& space, VmRange& range, size_t offset);

    /* TODO:
     */
    VmStatus SpaceLookup(VmRange** found, VmSpace& space, 
        uintptr_t addr);

    /* TODO:
     */
    VmStatus SpaceClone(VmSpace** clone, VmSpace& source);

    /* TODO:
     */
    VmStatus CreateMdlFromBuffer(Mdl** mdl, void* base, size_t length);

    /* TODO:
     */
    VmStatus CreateMdlFromInactiveBuffer(Mdl** mdl, VmSpace& space, void* base, 
        size_t length);

    /* TODO:
     */
    VmStatus CreateMdlFromPageList(Mdl** mdl, sl::Span<Paddr> pages, 
        size_t length, size_t offset);

    /* TODO:
     */
    VmStatus DestroyMdl(Mdl* mdl);

    /* Returns if the MDL has a window in kernel virtual memory. A window in
     * this context meaning the pages pointed to by the MDL mapped as contiguous
     * buffer in virtual memory.
     * Note that this function's return value is typically useless as the status
     * of the MDL's window may change as this function is returning. The return
     * value is 'best effort' and as such no hard decisions should be made based
     * on it.
     */
    bool IsMdlWindowValid(Mdl* mdl);

    /* This function ensures the MDL has a valid window, and increments the ref
     * count of it. A matching call to `MdlReleaseWindow()` is expected when the
     * callee of this function has finished with the window. Note that MDL
     * windows are exempt from typical page reclamation mechanisms so windows
     * should only be used where necessary. They are also potentially expensive 
     * to create and teardown.
     * Returns whether the window was successfully acquired or not. If not it
     * should not be accessed as it may not be mapped.
     */
    bool MdlAcquireWindow(Mdl* mdl);

    /* TODO:
     */
    void MdlReleaseWindow(Mdl* mdl);

    /* Returns the number of physical pages described by a memory descriptor
     * list.
     */
    size_t MdlPageCount(Mdl* mdl);
}
