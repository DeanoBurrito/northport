#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Npl
{
    constexpr uintptr_t DontCare = 0;

    enum class MemoryType
    {
        Usable,
        Reclaimable,
        KernelModules,
    };

    void InitMemoryManager();
    void EnableMmu();

    void* MapMemory(size_t length, MemoryType type, uintptr_t vaddr = DontCare, uintptr_t paddr = DontCare);
}
