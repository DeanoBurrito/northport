#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Npk
{
    struct [[gnu::packed]] RteFrame
    {
        uint16_t sr;
        uint32_t pc;
        uint16_t format : 4;
        uint16_t vector : 12;

        union
        {
            struct
            { 
                uint32_t address;
            } format2;

            struct
            {
                uint32_t address;
            } format3;

            struct
            {
                uint32_t address;
                uint32_t fslw;
            } format4;

            struct
            {
                uint32_t effectiveAddr;
                uint16_t ssw;
                uint16_t wb3s;
                uint16_t wb2s;
                uint16_t wb1s;
                uint32_t faultAddr;
                uint32_t wb3a;
                uint32_t wb3d;
                uint32_t wb2a;
                uint32_t wb2d;
                uint32_t wb1a;
                union
                {
                    uint32_t wb1d;
                    uint32_t pd0;
                };
                uint32_t pd1;
                uint32_t pd2;
                uint32_t pd3;
            } format7;
        };
    };

    struct TrapFrame
    {
        uint32_t d0;
        uint32_t d1;
        uint32_t d2;
        uint32_t d3;
        uint32_t d4;
        uint32_t d5;
        uint32_t d6;
        uint32_t d7;
        uint32_t a0;
        uint32_t a1;
        uint32_t a2;
        uint32_t a3;
        uint32_t a4;
        uint32_t a5;
        uint32_t a6;
        uint32_t a7;

        RteFrame rte;
    };

    static_assert(sizeof(TrapFrame) == 124, "m68k TrapFrame size changed, update assembly sources.");

    constexpr inline size_t PageSize = 0x1000;
    constexpr inline size_t TrapFrameArgCount = 6;
    constexpr inline size_t IntVectorAllocBase = 0x40;
    constexpr inline size_t IntVectorAllocLimit = 0xFF;

    [[gnu::always_inline]]
    inline void Wfi()
    { asm("stop #0x2000"); }

    [[gnu::always_inline]]
    inline bool InterruptsEnabled()
    { return true; }

    [[gnu::always_inline]]
    inline void EnableInterrupts()
    {}

    [[gnu::always_inline]]
    inline void DisableInterrupts()
    {}

    struct CoreLocalInfo;
    extern CoreLocalInfo* coreLocalControl; //TODO: SMP for m68k platforms?

    [[gnu::always_inline]]
    inline CoreLocalInfo& CoreLocal()
    {
        return *coreLocalControl;
    }

    [[gnu::always_inline]]
    inline bool CoreLocalAvailable()
    {
        return coreLocalControl != nullptr;
    }

    [[gnu::always_inline]]
    inline void WriteSr(uint32_t data)
    { 
        asm volatile("move %0, %%sr" :: "d"(data) : "cc");
    }

    [[gnu::always_inline]]
    inline uint32_t ReadSr()
    {
        uint32_t value;
        asm volatile("move %%sr, %0" : "=d"(value) :: "memory");
        return value;
    }
}
