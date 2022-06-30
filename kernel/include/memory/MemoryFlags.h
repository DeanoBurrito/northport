#pragma once

#include <stddef.h>

namespace Kernel::Memory
{
    /*
        These flags are meant to be os-specific, not platform-specific.
        They are also used both by the vmm, and by the abstract paging implementation.
    */
    enum class MemoryMapFlags : size_t
    {
        None = 0,

        //enables writing to a region of memory
        AllowWrites = 1 << 0,
        //enables executing a region of memory
        AllowExecute = 1 << 1,
        //enables user-level accesses
        UserAccessible = 1 << 2,
        //prevents user-level code configuring this memory within the vmm
        SystemRegion = 1 << 3,
        //lets a range be added, but ensures it will never be backed with physical ram
        ForceUnmapped = 1 << 4,
        //range that maps physical ram owned by another vmm
        ForeignMemory = 1 << 5,
    };

}

using MFlags = Kernel::Memory::MemoryMapFlags;
