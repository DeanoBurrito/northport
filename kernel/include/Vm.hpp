#pragma once

#include <Core.hpp>

namespace Npk
{
    using HeapTag = uint32_t;

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

    /* Attempts to allocate `len` bytes from the non-paged heap. Returns a
     * `nullptr` on failure to allocate or if `len` is 0, and a valid pointer 
     * otherwise.
     * If successful the allocation is tagged with the value of `tag`,
     * and `timeout` can be used to specify the maximum amount of time this
     * allocation should wait to acquire the lock, this timeout does not take
     * into account time spent manipulating internal structures to perform
     * the allocation.
     */
    void* HeapAllocNonPaged(size_t len, HeapTag tag, sl::TimeCount timeout 
        = sl::NoTimeout);

    /* Frees a previously allocated range of non-paged heap memory: `ptr`
     * must be a value previously returned from `HeapAllocNonPaged()`, and
     * `len` must be the same value passed to that function.
     * The `timeout` can be used to specify how long this function should wait
     * on any locks (at most one) in the freeing path. If the timeout passes
     * and the memory couldn't be freed, this function returns false. Otherwise
     * true is returned on successful freeing. False can only be returned
     * when timeout != sl::NoTimeout.
     */
    bool HeapFreeNonPaged(void* ptr, size_t len, HeapTag tag, 
        sl::TimeCount timeout = sl::NoTimeout);

    /* Attempts to allocate `len` bytes from the paged heap. Allocations here
     * may only be accessed at Ipl::Passive, unless pinned or held by a MDL.
     * If successful the allocation is tagged with the value of `tag`,
     * and `timeout` can be used to specify the maximum amount of time this
     * allocation should wait to acquire the lock, this timeout does not take
     * into account time spent manipulating internal structures to perform
     * the allocation.
     * Returns `nullptr` on failure or if `len` is 0, and a valid pointer 
     * otherwise.
     */
    void* HeapAlloc(size_t len, HeapTag tag, sl::TimeCount timeout 
        = sl::NoTimeout);

    /* Frees memory from the paged heap: `ptr` must have originated from an
     * earlier call to `HeapAlloc()`, and `len` must be same value passed to
     * the same call to `HeapAlloc()`.
     * The `timeout` can be used to specify how long this function should wait
     * on any locks (at most one) in the freeing path. If the timeout passes
     * and the memory couldn't be freed, this function returns false. Otherwise
     * true is returned on successful freeing. False can only be returned
     * when timeout != sl::NoTimeout.
     */
    bool HeapFree(void* ptr, size_t len, HeapTag tag, 
        sl::TimeCount timeout = sl::NoTimeout);

    /* Attempts to allocate a range of `length` bytes in address space `space`,
     * with the base address aligned to the value of `align`. If 
     * VmStatus::Success is returned the allocated address is placed in `*addr`,
     * otherwise `*addr` is unchanged. If `*addr` is non-null, it is taken
     * as the requested base address, if this address is unavailable, this
     * function returns VmStatus::InUse.
     */
    VmStatus AllocateInSpace(VmSpace& space, void** addr, size_t length, 
        size_t align);

    /* Releases `length` bytes of address space from `base` in address space
     * `space`, making them available for future allocations. Note this
     * address space may not be immediately available for reuse. This function
     * does not take care of unmapping anything in this space.
     */
    VmStatus FreeInSpace(VmSpace& space, void* base, size_t length);
}
