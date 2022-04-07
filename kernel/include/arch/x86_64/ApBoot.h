#pragma once

#include <stdint.h>

namespace Kernel
{
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
        SmpCoreInfo cores[];
    };
    
    //returns the address of the global config
    SmpInfo* BootAPs();
    void ApBootCleanup();
}