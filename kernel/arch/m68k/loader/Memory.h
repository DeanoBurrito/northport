#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Npl
{
    constexpr uintptr_t DontCare = 0;
    constexpr uintptr_t HhdmBase = 0x8000'0000;

    enum class MemoryType
    {
        Usable,
        Reclaimable,
        KernelModules,
    };

    void InitMemoryManager();
    void EnableMmu();
    size_t HhdmLimit();

    void* MapMemory(size_t length, uintptr_t vaddr, uintptr_t paddr = DontCare);
    uintptr_t GetMap(uintptr_t vaddr);
}
