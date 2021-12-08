#pragma once

#include <stdint.h>
#include <stddef.h>

#define GDT_NULL 0x00
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE 0x18
#define GDT_USER_DATA 0x20

namespace Kernel
{
    extern uint64_t defaultGdt[];

    struct [[gnu::packed]] GDTR
    {
        uint16_t limit;
        uint64_t address;
    };

    struct [[gnu::packed]] TssEntry
    {
        uint32_t values[100]; //ah yes, nicely done there amd.

        void SetIST(size_t index, uint64_t value);
        uint64_t GetIst(size_t index) const;
        void SetRSP(size_t privilegeLevel, uint64_t value);
        uint64_t GetRSP(size_t privilegeLevel) const;
    };

    void SetupGDT();
    
    [[gnu::naked]]
    void FlushGDT();
}