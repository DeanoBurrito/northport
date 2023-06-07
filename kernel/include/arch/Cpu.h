#pragma once

namespace Npk
{
    enum class CpuFeature
    {
        VGuest,
#ifdef __x86_64__
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
#elif defined(__riscv)
        Sstc,
        SingleFPU,
        DoubleFPU,
        QuadFPU,
#endif
    };

    void ScanCpuFeatures();
    void LogCpuFeatures();
    bool CpuHasFeature(CpuFeature feature);
    const char* CpuFeatureName(CpuFeature feature);
}
