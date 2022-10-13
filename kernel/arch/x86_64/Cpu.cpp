#include <arch/Cpu.h>
#include <debug/Log.h>
#include <Memory.h>
#include <Maths.h>
#include <stdint.h>

//NOTE: this is a non-standard header, but any reputable compiler vendor should ship one.
#include <cpuid.h>

namespace Npk
{
    struct CpuidLeaf
    {
        uint32_t a;
        uint32_t b;
        uint32_t c;
        uint32_t d;
    };

    constexpr unsigned BaseLeafLimit = 8;
    constexpr unsigned ExtLeafLimit = 8;
    CpuidLeaf baseLeaves[BaseLeafLimit];
    CpuidLeaf extLeaves[ExtLeafLimit];

    void ScanCpuFeatures()
    {
        for (size_t i = 0; i < BaseLeafLimit; i++)
            baseLeaves[i] = { 0, 0, 0, 0 };
        for (size_t i = 0; i < ExtLeafLimit; i++)
            extLeaves[i] = { 0, 0, 0, 0 };
        
        const size_t baseLeafCount = sl::Min((unsigned)__get_cpuid_max(0, nullptr), BaseLeafLimit);
        const size_t extLeafCount = sl::Min((unsigned)__get_cpuid_max(0x8000'0000, nullptr), ExtLeafLimit);

        for (size_t i = 0; i < baseLeafCount; i++)
            __get_cpuid_count(i, 0, &baseLeaves[i].a, &baseLeaves[i].b, &baseLeaves[i].c, &baseLeaves[i].d);
        for (size_t i = 0; i < extLeafCount; i++)
            __get_cpuid_count(0x8000'0000 + i, 0, &extLeaves[i].a, &extLeaves[i].b, &extLeaves[i].c, &extLeaves[i].d);
    }

    void LogCpuFeatures()
    {
        const unsigned basicCount = __get_cpuid_max(0, nullptr);
        const unsigned extCount = __get_cpuid_max(0x8000'0000, nullptr);
        uint32_t a = 0, b = 0, c = 0, d = 0;
        __get_cpuid(0, &a, &b, &c, &d);
        char vendorStr[13];
        for (size_t i = 0; i < 4; i++)
        {
            const size_t shiftor = i * 8;
            vendorStr[0 + i] = (b >> shiftor) & 0xFF;
            vendorStr[4 + i] = (d >> shiftor) & 0xFF;
            vendorStr[8 + i] = (c >> shiftor) & 0xFF;
        }
        vendorStr[12] = 0;

        Log("CPUID: basicCount=%u, extendedCount=%u, vendor=%s", LogLevel::Info,
            basicCount, extCount - 0x8000'0000, vendorStr);
        if (extCount >= 0x8000'0004)
        {
            char brandStr[48];
            sl::memset(brandStr, 0, 48);
            for (size_t reg = 0; reg < 3; reg++)
            {
                __get_cpuid(0x8000'0002 + reg, &a, &b, &c, &d);
                const size_t offset = reg * 16;

                for (size_t i = 0; i < 4; i++)
                {
                    const size_t shiftor = i * 8; //haha, manual sse :(
                    brandStr[offset + 0 + i] = (a >> shiftor) & 0xFF;
                    brandStr[offset + 4 + i] = (b >> shiftor) & 0xFF;
                    brandStr[offset + 8 + i] = (c >> shiftor) & 0xFF;
                    brandStr[offset + 12 + i] = (d >> shiftor) & 0xFF;
                }
            }
            Log("CPUID: brand=%s", LogLevel::Info, brandStr);
        }

#ifdef NP_DUMP_CPUID
        for (size_t i = 0; i < basicCount; i++)
        {
            __get_cpuid_count(i, 0, &a, &b, &c, &d);
            Log("CPUID %2lu: a=0x%08x, b=0x%08x, c=0x%08x, d=0x%08x", LogLevel::Verbose,
                i, a, b, c, d);
        }

        for (size_t i = 0x8000'0000; i < extCount; i++)
        {
            __get_cpuid_count(i, 0, &a, &b, &c, &d);
            Log("CPUID ext %2lu: a=0x%08x, b=0x%08x, c=0x%08x, d=0x%08x", LogLevel::Verbose,
                i & ~0x8000'0000, a, b, c, d);
        }
#endif
    }

    bool CpuHasFeature(CpuFeature feature)
    {
        switch (feature)
        {
        case CpuFeature::NoExecute: 
            return extLeaves[1].d & (1 << 20);
        case CpuFeature::Pml3Translation: 
            return extLeaves[1].d & (1 << 26);
        case CpuFeature::GlobalPages: 
            return extLeaves[1].d & (1 << 13);
        case CpuFeature::Smap: 
            return baseLeaves[7].b & (1 << 20);
        case CpuFeature::Smep: 
            return baseLeaves[7].b & (1 << 7);
        case CpuFeature::Umip: 
            return baseLeaves[7].c & (1 << 2);
        case CpuFeature::Apic: 
            return baseLeaves[1].d & (1 << 9);
        case CpuFeature::ApicX2: 
            return baseLeaves[1].c & (1 << 21);
        case CpuFeature::FxSave: 
            return baseLeaves[1].d & (1 << 24);
        case CpuFeature::XSave: 
            return baseLeaves[1].c & (1 << 26);
        case CpuFeature::FPU: 
            return baseLeaves[1].d & (1 << 0);
        case CpuFeature::SSE: 
            return baseLeaves[1].d & (1 << 25);
        case CpuFeature::SSE2: 
            return baseLeaves[1].d & (1 << 26);
        case CpuFeature::VGuest:
            return baseLeaves[1].c & (1 << 31);
        case CpuFeature::AlwaysRunningApic:
            return baseLeaves[6].a & (1 << 2);
        case CpuFeature::Tsc:
            return baseLeaves[1].d & (1 << 4);
        case CpuFeature::TscDeadline:
            return baseLeaves[1].c & (1 << 24);
        case CpuFeature::InvariantTsc:
            return extLeaves[7].d & (1 << 8);
        
        default:
            return false;
        }
    }

    const char* CpuFeatureName(CpuFeature feature)
    {
        switch (feature)
        {
        case CpuFeature::NoExecute:
            return "nx";
        case CpuFeature::Pml3Translation:
            return "pml3t";
        case CpuFeature::GlobalPages:
            return "globalpg";
        case CpuFeature::Smap:
            return "smap";
        case CpuFeature::Smep:
            return "smep";
        case CpuFeature::Umip:
            return "umip";
        case CpuFeature::Apic:
            return "lapic";
        case CpuFeature::ApicX2:
            return "x2apic";
        case CpuFeature::FxSave:
            return "fxsave";
        case CpuFeature::XSave:
            return "xsave";
        case CpuFeature::FPU:
            return "fpu";
        case CpuFeature::SSE:
            return "sse";
        case CpuFeature::SSE2:
            return "sse2";
        case CpuFeature::VGuest:
            return "vguest";
        case CpuFeature::AlwaysRunningApic:
            return "arat";
        case CpuFeature::Tsc:
            return "tsc";
        case CpuFeature::TscDeadline:
            return "tscd";
        case CpuFeature::InvariantTsc:
            return "itsc";
        default:
            return "";
        }
    }
}
