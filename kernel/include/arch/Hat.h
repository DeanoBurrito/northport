#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Optional.h>

/*
    The HAT (hardware address and translation) represents the MMU or whatever
    hardware is used to manage virtual memory. We're assuming paging on most accounts,
    but the interface is abstract enough that segmentation should also be a viable option,
    or some other esoteric method.

    The HAT API is intended for use mainly by the VMM, and not by other kernel subsystems.
*/

#if defined(__x86_64__)
    #include <arch/x86_64/Hat.h>
#elif __riscv_xlen == 64
    #include <arch/riscv64/Hat.h>
#else
    #error "Compiling for unsupported ISA."
#endif

namespace Npk
{
    constexpr HatFlags operator|(const HatFlags& a, const HatFlags& b)
    { return (HatFlags)((size_t)a | (size_t)b); }

    constexpr HatFlags operator|=(HatFlags& src, const HatFlags& other)
    { return src = (HatFlags)((uintptr_t)src | (uintptr_t)other); }

    constexpr HatFlags operator&(const HatFlags& a, const HatFlags& b)
    { return (HatFlags)((size_t)a & (size_t)b); }

    constexpr HatFlags operator&=(HatFlags& src, const HatFlags& other)
    { return src = (HatFlags)((uintptr_t)src & (uintptr_t)other); }

    /*
        This struct holds implementation-specific details, and we only pass pointers to
        this struct through the API. This is intentional so the inner workings of the HAT
        are opaque to other kernel subsystems.
    */
    struct HatMap;

    //check that the bare minimum of flags are defined for the target platform:
    //these are needed for the kernel to function.
    static_assert(HatFlags::None == HatFlags::None, "HatFlags::None not defined");
    static_assert(HatFlags::Write == HatFlags::Write, "HatFlags::Write not defined");
    static_assert(HatFlags::User == HatFlags::User, "HatFlags::User not defined");
    static_assert(HatFlags::Global == HatFlags::Global, "HatFlags::Global not defined");
    static_assert(HatFlags::Execute == HatFlags::Execute, "HatFlags::Execute not defined");

    constexpr size_t MaxHatModes = 8;
    //This struct is used to communicate the limits of the underlying MMU to the
    //rest of the kernel.
    struct HatLimits
    {
        size_t modeCount;
        struct 
        {
            size_t granularity;
        } modes[MaxHatModes];
    };

    //hook to perform some init based on the MMU's capabilities if needed.
    void HatInit();

    //returns the modes supported by the current MMU.
    const HatLimits& GetHatLimits();

    //creates a new address space (without loading it).
    HatMap* InitNewMap();
    
    //gets the kernel map
    HatMap* KernelMap();

    //creates a virt <-> phys mapping in an address space.
    bool Map(HatMap* map, uintptr_t vaddr, uintptr_t paddr, size_t mode, HatFlags flags, bool flush);

    //attempts to remove an existing mapping from an address space.
    //NOTE: paddr and mode are references and return the previously used values.
    bool Unmap(HatMap* map, uintptr_t vaddr, uintptr_t& paddr, size_t& mode, bool flush);

    //attempts to return the physical address of 
    sl::Opt<uintptr_t> GetMap(HatMap* map, uintptr_t vaddr);

    //hook that checks a userspace mapping has up-to-date kernel mappings,
    //this is run immediately before loading a new map.
    void SyncWithMasterMap(HatMap* map);

    //replaces the currently active HAT address space with this one.
    void MakeActiveMap(HatMap* map);

    //hook for getting the MMU into a known-good state during the panic sequence.
    void HatHandlePanic() asm("HatHandlePanic");
}
