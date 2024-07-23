#pragma once

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
        Count
    };

    bool CpuHasFeature(CpuFeature feature);
}
