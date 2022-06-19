#pragma once

#include <stdint.h>

namespace Kernel
{
    /*
        Yes, technically APIC refers to the x86 core-local interrupt controller,
        but it's being used as a general term here. Where an apic id just represents the id
        of the platform-specific (ACLINT/GIC/whatever) controller.
    */
    constexpr uint64_t AP_BOOT_APIC_ID_END = (uint64_t)-1;
    constexpr uint64_t AP_BOOT_APIC_ID_INVALID = (uint64_t)-2;
    
    struct SmpCoreInfo
    {
        uint64_t apicId;
        uint64_t gotoAddress;
        uint64_t stack;
        uint64_t acpiProcessorId;
    };
    
    struct SmpInfo
    {
        uint64_t bspApicId;

        //these are here to keep the memory layout simpler, not for external use really.
        struct
        {
            uint64_t cr3;
            uint64_t gdtr;
            //bit 0 = use nx, bit 1 = use la57
            uint64_t flags;
        } bootstrapDetails;

        /*
            This is a sparse-array. To keep things simple for the assembly stub
            running on the remote core, we use the apic id as an index into this array.
            Meaning if an apic id is unused, but there are higher ids that are, there
            will be bubbles/empty elements in this array.
            The magic values above (APIC_ID_END and APIC_ID_INVALID) indicate the end
            and invalid (to be ignored) elements, respectively.
        */
        SmpCoreInfo cores[];
    };
    
    //returns the address of the global config
    SmpInfo* BootAPs();
    void ApBootCleanup();
}
