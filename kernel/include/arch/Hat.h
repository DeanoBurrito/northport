#pragma once

#include <arch/__Select.h>
#include <Optional.h>
#include <Flags.h>
#include <Error.h>

/*
    The HAT (hardware address and translation) represents the MMU or whatever
    hardware is used to manage virtual memory. We're assuming paging on most accounts,
    but the interface is abstract enough that segmentation should also be a viable option,
    or some other esoteric method.
*/

#ifdef NPK_ARCH_INCLUDE_HAT
#include NPK_ARCH_INCLUDE_HAT
#endif

namespace Npk
{
    enum class HatFlag
    {
        Write,
        Execute,
        User,
        Global,
        Mmio,
        Framebuffer,
    };

    using HatFlags = sl::Flags<HatFlag>;

    constexpr size_t MaxHatModes = 8;
    //This struct is used to communicate the limits of the underlying MMU to the
    //rest of the kernel.
    struct HatLimits
    {
        bool flushOnPermsUpgrade;
        bool hwTlbBroadcast;
        size_t modeCount;
        struct 
        {
            size_t granularity;
        } modes[MaxHatModes];
    };

    /*
        This struct holds implementation-specific details, and we only pass pointers to
        this struct through the API. This is intentional so the inner workings of the HAT
        are opaque to other kernel subsystems.
    */
    struct HatMap;

    enum class HatError
    {
        Success = 0,
        InvalidArg,
        PmAllocFailed,
        MapAlreadyExists,
        NoExistingMap,
    };

    //a hook for the arch layer to perform some global mmu detection and setup. This function
    //is also responsible for mapping the unchanging regions of the kernel map:
    // - the kernel image
    // - the HHDM
    // - the virtually contiguous PageInfo database
    void HatInit();

    //returns the modes supported by the current MMU.
    const HatLimits& HatGetLimits();

    //creates a new address space (without loading it).
    HatMap* HatCreateMap();

    //destroys (and frees) structures for an existing address space.
    void HatDestroyMap(HatMap* map);
    
    //gets the kernel map
    HatMap* KernelMap();

    //creates a virt <-> phys mapping in an address space.
    HatError HatDoMap(HatMap* map, uintptr_t vaddr, uintptr_t paddr, size_t mode, HatFlags flags, bool flush);

    //attempts to remove an existing mapping from an address space.
    //NOTE: paddr and mode are references and return the previously used values.
    HatError HatDoUnmap(HatMap* map, uintptr_t vaddr, uintptr_t& paddr, size_t& mode, bool flush);

    //attempts to return the physical address and size of a mapping
    sl::ErrorOr<uintptr_t, HatError> HatGetMap(HatMap* map, uintptr_t vaddr, size_t& mode);

    //attempts to update an existing mapping: either flags, physical address of both.
    HatError HatSyncMap(HatMap* map, uintptr_t vaddr, sl::Opt<uintptr_t> paddr, sl::Opt<HatFlags> flags, bool flush);

    //if supported (see modes.hwTlbBroadcast), performs a hardware-assisted flush of all TLBs for a particular vaddr.
    bool HatFlushBroadcast(uintptr_t vaddr);

    //attempts to flush a cached mapping from the local translation cache.
    void HatFlushMap(uintptr_t vaddr);

    //replaces the currently active HAT address space with this one.
    void HatMakeActive(HatMap* map, bool supervisor);
}
