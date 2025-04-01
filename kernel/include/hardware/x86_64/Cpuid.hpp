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

    CpuidLeaf& DoCpuid(uint32_t leaf, uint32_t subleaf, CpuidLeaf& data);

    bool CpuHasFeature(CpuFeature feature);
    void LogCpuFeatures();
}
