#pragma once

#include <Vm.hpp>
#include <Hardware.hpp>

namespace Npk
{
    struct NsObject;
}

namespace Npk::Private
{
    constexpr auto VmSourceTag = NPK_MAKE_HEAP_TAG("VSrc");

    MmuFlags VmToMmuFlags(VmFlags flags, MmuFlags extra);

    NpkStatus CreateAnonPage(AnonPage** page);
    void DestroyAnonPage(AnonPage* page);

    NpkStatus CreateAnonMap(AnonMap** map, size_t slotCount);
    void DestroyAnonMap(AnonMap* map);
    NpkStatus ResizeAnonMap(AnonMap& map, size_t newSlotCount);
    AnonPageRef AnonMapLookup(AnonMap& map, size_t slot);
    void AnonMapAdd(AnonMap& map, size_t slot, AnonPageRef& anon);
    AnonPageRef AnonMapRemove(AnonMap& map, size_t slot);

    VmSource* AnonSourceAttach(size_t size);
    VmSource* NamedSourceAttach(NsObject& obj);
    //VmSource* DeviceSourceAttach(); TODO: revisit after driver subsystem

    sl::Opt<Paddr> AllocatePageTable(size_t level);
    void FreePageTable(size_t level, Paddr paddr);

    void InitPool(uintptr_t base, size_t length);
    void* PoolAlloc(size_t len, HeapTag tag, bool paged, sl::TimeCount timeout 
        = sl::NoTimeout);
    bool PoolFree(void* ptr, size_t len, HeapTag tag, bool paged, 
        sl::TimeCount timeout = sl::NoTimeout);
}
