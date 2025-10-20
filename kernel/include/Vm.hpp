#pragma once

#include <Types.hpp>
#include <Flags.hpp>

namespace Npk
{
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
     * a virtual address without marking the final level as valid. This allows
     * later code to complete the mapping by only adjusting the bits of the 
     * final PTE.
     */
    VmStatus PrimeKernelMap(uintptr_t vaddr);

    /* Sets the mapping for kernel translations at `vaddr` to target `paddr`.
     * Note that this is a very low level routine, and skips a lot of sanity
     * checks and higher features of the memory subsystem.
     */
    VmStatus SetKernelMap(uintptr_t vaddr, Paddr paddr, VmFlags flags);
}
