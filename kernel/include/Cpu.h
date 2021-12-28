#pragma once

#include <stdint.h>
#include <stddef.h>

namespace Kernel
{
    enum class CpuFeature : unsigned
    {
        ExecuteDisable,
        GigabytePages,
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

    //real definition in Platform.h
    struct CpuFrequencies;
    
    class CPU
    {
    private:

    public:
        static void DoCpuId();

        static void SetupExtendedState();
        static size_t GetExtendedStateBufferSize();
        static void SaveExtendedState(uint8_t* buff);
        static void LoadExtendedState(uint8_t* buff);

        static bool InterruptsEnabled();
        static void SetInterruptsFlag(bool state = true);
        static void ClearInterruptsFlag();

        static void PortWrite8(uint16_t port, uint8_t data);
        static void PortWrite16(uint16_t port, uint16_t data);
        static void PortWrite32(uint16_t port, uint32_t data);
        static uint8_t PortRead8(uint16_t port);
        static uint16_t PortRead16(uint16_t port);
        static uint32_t PortRead32(uint16_t port);

        static void WriteMsr(uint32_t address, uint64_t data);
        static uint64_t ReadMsr(uint32_t address);

        static bool FeatureSupported(CpuFeature feature);
        static const char* GetFeatureStr(CpuFeature feature, bool getFullname = false);

        static const char* GetVendorString();
        static const CpuFrequencies GetFrequencies();
    };
}
