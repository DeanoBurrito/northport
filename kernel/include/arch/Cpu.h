#pragma once

#include <stdint.h>
#include <stddef.h>

namespace Kernel
{
    enum class CpuFeature : unsigned
    {
        ExecuteDisable,
        GigabytePages,
        GlobalPages,
        SMAP,
        SMEP,
        UMIP,
        APIC,
        X2APIC,

        FXSave,
        XSave,

        FPU,
        SSE,
        SSE2,
        SSE3,
        S_SSE3,
        SSE4_1,
        SSE4_2,
        SSE4A,
        AVX,

        EnumCount
    };

    //if any values are pinned at (uint32_t)-1, they're invalid
    struct CpuFrequencies
    {
        uint32_t coreClockBaseHertz;
        uint32_t coreMaxBaseHertz;
        uint32_t coreTimerHertz; //TSC freq on x86
        uint32_t busClockHertz;
    };
    
    class CPU
    {
    private:

    public:
        static void DoCpuId();
        static void Halt();

        static bool HasExtenedState();
        static void SetupExtendedState();
        static size_t GetExtendedStateBufferSize();
        static void SaveExtendedState(uint8_t* buff);
        static void LoadExtendedState(uint8_t* buff);

        static bool InterruptsEnabled();
        static void EnableInterrupts(bool state = true);
        static void DisableInterrupts();

        static bool FeatureSupported(CpuFeature feature);
        static const char* GetFeatureStr(CpuFeature feature, bool getFullname = false);

        static const char* GetVendorString();
        static const CpuFrequencies GetFrequencies();

        //SMA/Supervisor memory access (to user mapped pages) functions. Either SMAP (x86) or SUM (riscv).
        static void AllowSumac(bool allowed);
        static bool SumacAllowed();
    };
}
