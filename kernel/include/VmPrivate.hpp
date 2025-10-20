#pragma once

#include <Vm.hpp>
#include <Core.hpp>
#include <Hardware.hpp>

namespace Npk::Private
{
    MmuFlags VmToMmuFlags(VmFlags flags, MmuFlags extra);

    sl::Opt<Paddr> AllocatePageTable(size_t level);
    void FreePageTable(size_t level, Paddr paddr);

    VmStatus PrimeMapping(HwMap map, uintptr_t vaddr, MmuWalkResult& result, PageAccessRef& ref);
    VmStatus SetMap(HwMap map, uintptr_t vaddr, Paddr paddr, VmFlags flags);
    VmStatus ClearMap(HwMap map, uintptr_t vaddr);
}
