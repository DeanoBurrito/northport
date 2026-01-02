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

        /* If set, memory will backed immediately and will not be paged out.
         */
        Bound,

        /* If set, operations on this memory will fail if they need to allocate
         * any resources.
         */
        NoAllocate,
    };

    using VmFlags = sl::Flags<VmFlag>;

    struct VmSpace;

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
     * or indiret translations) to `paddr`. A direct translation one performed 
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

    VmStatus LookupRangeInSpace(VmRange** found, VmSpace& space, uintptr_t addr);
}
