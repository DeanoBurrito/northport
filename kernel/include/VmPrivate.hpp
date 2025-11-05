#pragma once

#include <Vm.hpp>
#include <Hardware.hpp>

namespace Npk::Private
{
    MmuFlags VmToMmuFlags(VmFlags flags, MmuFlags extra);

    sl::Opt<Paddr> AllocatePageTable(size_t level);
    void FreePageTable(size_t level, Paddr paddr);

    void InitPool(uintptr_t base, size_t length);
    void* PoolAlloc(size_t len, HeapTag tag, bool paged, sl::TimeCount timeout 
        = sl::NoTimeout);
    bool PoolFree(void* ptr, size_t len, bool paged, sl::TimeCount timeout 
        = sl::NoTimeout);
}
