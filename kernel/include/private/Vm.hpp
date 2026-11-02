#pragma once

#include "../Vm.hpp"
#include <Hardware.hpp>

namespace Npk
{
    /* Forward declaration for `NamedSourceAttach()`.
     */
    struct NsObject;

    NpkStatus SpaceLookupLocked(VmRange** found, VmSpace& space, uintptr_t addr,
        size_t length);

    void PinPage(PageInfo* page);
    void UnpinPage(PageInfo* page, size_t count = 1);
}

namespace Npk::Private
{
    constexpr auto VmSourceTag = NPK_MAKE_HEAP_TAG("VSrc");

    enum class TrimPressure
    {
        None,
        Reclaim,
        LowLevel,
        MinLevel,
    };

    LiveListHead& GetLiveListHead(SystemDomain& dom, LiveList which);
    //NOTE: assumes `dom.liveLists.lock` is held
    void LiveListLink(SystemDomain& dom, LiveList which, PageInfo* page);
    //NOTE: assumes `dom.liveLists.lock` is held
    void LiveListUnlink(SystemDomain& dom, PageInfo* page);

    SL_ALWAYS_INLINE
    LiveList IdToLiveList(uint8_t num)
    {
        return static_cast<LiveList>(num - 1);
    }

    SL_ALWAYS_INLINE
    uint8_t LiveListToId(LiveList list)
    {
        return static_cast<uint8_t>((size_t)list + 1);
    }

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
    VmSource* ModuleSourceAttach();
    //VmSource* DeviceSourceAttach(); TODO: revisit after driver subsystem

    bool ModuleSourceAvailable();
    NpkStatus InitModuleSource(uint64_t seed, size_t failPercent,
        size_t maxDelayUs);
    NpkStatus InitStackCaches();

    /* Backend of `FreeKenrelStack()`, `keepPerCpu` is the float count.
     * Returns number of stacks freed.
     */
    size_t TrimKernelStackCaches(size_t keepPerCpu, sl::TimeCount timeout);

    void PinPage(PageInfo* page);
    void UnpinPage(PageInfo* page, size_t count = 1);
    void TrackPage(PageInfo* page); //places page on active list
    void UntrackPage(PageInfo* page); //removes page from a live list (if any)
    void DrainSourcePages(VmSource& src);

    void ClearPageMapped(PageInfo* page);
    void BeginMappingRemovals(VmRange& range);
    void EndMappingRemovals(VmRange& range);
    void SettleAnonPage(AnonMap& map, PageInfo* page, bool dirtied,
        bool unpin);
    void SettleSourcePage(VmSource& src, PageInfo* page, bool dirtied,
        bool unpin);
    NpkStatus SettleMappedPage(Paddr paddr, bool dirtied);

    /* Runs once pass of page trimming, exposed here for testing purposes only.
     */
    size_t TrimOnce(size_t batch, bool ignoreRefs = false,
        bool exactRefs = false, TrimPressure pressure = TrimPressure::Reclaim);
}
