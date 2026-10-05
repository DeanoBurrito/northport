#pragma once

#include "../Vm.hpp"
#include <Hardware.hpp>

namespace Npk
{
    struct NsObject;

    NpkStatus SpaceLookupLocked(VmRange** found, VmSpace& space, uintptr_t addr,
        size_t length);

    void PinPage(PageInfo* page);
    void UnpinPage(PageInfo* page, size_t count = 1);
}

namespace Npk::Private
{
    constexpr auto VmSourceTag = NPK_MAKE_HEAP_TAG("VSrc");

    NpkStatus CreateAnonPage(AnonPage** page);
    void DestroyAnonPage(AnonPage* page);
    NpkStatus AnonPageGetPage(PageInfo** info, AnonPageRef page);

    NpkStatus CreateAnonMap(AnonMap** map, size_t slotCount);
    void DestroyAnonMap(AnonMap* map);
    NpkStatus ResizeAnonMap(AnonMap& map, size_t newSlotCount);
    AnonPageRef AnonMapLookup(AnonMap& map, size_t slot);
    AnonPageRef AnonMapLookupLocked(AnonMap& map, size_t slot);

    /* Links a range to an anon map, the range mutex may or may not held, but
     * the `map.mutex` must not be. Same applies to UnlinkRange().
     */
    NpkStatus AnonMapLinkRange(AnonMap& map, VmRange* range);
    NpkStatus AnonMapUnlinkRange(AnonMap& map, VmRange* range);

    NpkStatus AnonMapAdd(AnonMap& map, size_t slot, AnonPageRef& anon);
    AnonPageRef AnonMapRemove(AnonMap& map, size_t slot);
    NpkStatus AnonMapClone(AnonMapRef* clone, AnonMap& source);

    VmSource* AnonSourceAttach(size_t size);
    VmSource* NamedSourceAttach(NsObject& obj);
    //VmSource* DeviceSourceAttach(); TODO: revisit after driver subsystem

    sl::Opt<Paddr> AllocatePageTable(size_t level);
    void FreePageTable(size_t level, Paddr paddr);

    void InitPool(uintptr_t base, size_t length);
    void* PoolAlloc(size_t len, HeapTag tag, bool paged, sl::TimeCount timeout 
        = sl::NoTimeout);
    NpkStatus PoolFree(void* ptr, size_t len, HeapTag tag, bool paged, 
        sl::TimeCount timeout = sl::NoTimeout);
}
