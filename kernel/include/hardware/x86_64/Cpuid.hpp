#pragma once

#include <Types.h>

namespace Npk
{
    enum class CpuFeature : unsigned
    {
        VGuest,
        NoExecute,
        Pml3Translation,
        GlobalPages,
        Smap,
        Smep,
        Umip,
        Apic,
        ApicX2,
        FxSave,
        XSave,
        FPU,
        SSE,
        SSE2,
        AlwaysRunningApic,
        Tsc,
        TscDeadline,
        InvariantTsc,
        Pat,
        BroadcastInvlpg,
        Mtrr,
        PvClock,

        Count
    };

    struct CpuidLeaf
    {
        uint32_t b;
        uint32_t d;
        uint32_t c;
        uint32_t a;

        uint32_t operator[](uint8_t index)
        {
            switch (index)
            {
            case 'a': return a;
            case 'b': return b;
            case 'c': return c;
            case 'd': return d;
            default: return 0;
            }
        }
    };

    constexpr uint32_t BaseLeaf = 0;
    constexpr uint32_t HypervisorLeaf = 0x4000'0000;
    constexpr uint32_t ExtendedLeaf = 0x8000'0000;

    CpuidLeaf& DoCpuid(uint32_t leaf, uint32_t subleaf, CpuidLeaf& data);

    bool CpuHasFeature(CpuFeature feature);
    void LogCpuFeatures();
}
