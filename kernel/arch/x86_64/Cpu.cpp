#include <arch/Cpu.h>
#include <cpuid.h>
#include <Platform.h>
#include <Log.h>
#include <Memory.h>

namespace Kernel
{
    uint32_t leaf1Edx;
    uint32_t leaf1Ecx;
    uint32_t leaf7_0Ebx;
    uint32_t leaf7_0Ecx;
    uint32_t extLeaf1Edx;
    uint32_t extLeaf1Ecx;

    struct CpuExtendedStateDetails
    {
        bool setup;
        bool xSaveAvailable;
        size_t storeBufferSize;
    };

    CpuExtendedStateDetails extendedState;

    bool CPU::InterruptsEnabled()
    {
        uint64_t rflags;
        asm volatile("pushf; pop %0" : "=r"(rflags));
        return (rflags & 0b10'0000'0000) != 0;
    }

    void CPU::EnableInterrupts(bool state)
    {
        if (state)
            asm volatile("sti" ::: "cc");
        else
            asm volatile("cli" ::: "cc");
    }

    void CPU::DisableInterrupts()
    {
        asm volatile("cli" ::: "cc");
    }

    void CPU::DoCpuId()
    {
        extendedState.setup = false;
        uint32_t scratch = 0;
        const unsigned maxBaseLeaf = __get_cpuid_max(0, nullptr);
        const unsigned maxExtLeaf = __get_cpuid_max(0x8000'0000, nullptr);

        //cache these values for later, so we dont have to emit cpuid instructions (they're serializing).
        if (maxBaseLeaf >= 1)
            __get_cpuid(1, &scratch, &scratch, &leaf1Ecx, &leaf1Edx);
        else
            leaf1Ecx = leaf1Edx = 0;
        
        if (maxExtLeaf >= 1)
            __get_cpuid(0x8000'0001, &scratch, &scratch, &extLeaf1Ecx, &extLeaf1Edx);
        else
            extLeaf1Ecx = extLeaf1Edx = 0;

        if (maxBaseLeaf >= 7)
            __get_cpuid_count(7, 0, &scratch, &leaf7_0Ebx, &leaf7_0Ecx, &scratch);
        else
            leaf7_0Ebx = leaf7_0Ecx = 0;

    }

    void CPU::Halt()
    {
        asm volatile("hlt");
    }

    bool CPU::HasExtenedState()
    {
        return true;
    }

    void CPU::SetupExtendedState()
    {
        if (extendedState.setup)
            return;
        
        //ensure the baseline x86_64 spec is met (fpu, sse, sse2, fxsave)
        if (!FeatureSupported(CpuFeature::FPU))
        {
            Log("x86_64 must have an FPU available, cpuid reports it unavailable. Aborting extended cpu init.", LogSeverity::Error);
            return;
        }
        if (!FeatureSupported(CpuFeature::SSE))
        {
            Log("x86_64 cpu must support SSE, cpuid reports it unavailable. Aborting extended cpu init.", LogSeverity::Error);
            return;
        }
        if (!FeatureSupported(CpuFeature::SSE2))
        {
            Log("x86_64 must support SSE2 available, cpuid reports it unavailable. Aborting extended cpu init.", LogSeverity::Error);
            return;
        }
        if (!FeatureSupported(CpuFeature::FXSave))
        {
            Log("x86_64 must support FXSAVE/FXRSTOR, cpuid reports it unavailable. Aborting extended cpu init.", LogSeverity::Error);
            return;
        }

        extendedState.xSaveAvailable = FeatureSupported(CpuFeature::XSave) && false; //TODO: XSave support

        //we've met the expected feature set, we'll check for anything else we might need to init as we go
        uint64_t cr0 = ReadCR0();
        cr0 |= (1 << 1); //set bit 1, Monitor coProcessor/MP
        cr0 &= ~(3 << 2); //clear bits 2 & 3 (Task Switched and Emulate FPU flags), means we must perform the context switch ourselves
        cr0 |= (1 << 5); //enable FPU exceptions
        WriteCR0(cr0);

        uint64_t cr4 = ReadCR4();
        cr4 |= (3 << 9); //enable bits 9 & 10: os supports fxsave/fxrstor, and simd exceptions
        WriteCR4(cr4);

        asm volatile("finit");

        if (!extendedState.xSaveAvailable)
            extendedState.storeBufferSize = 512; //fxsave uses fixed 512 byte buffer
        else
        {} 

        Logf("Extended cpu state setup for core %lu", LogSeverity::Verbose, CoreLocal()->id);
        extendedState.setup = true;
    }

    size_t CPU::GetExtendedStateBufferSize()
    { return extendedState.setup ? extendedState.storeBufferSize : 0; }

    void CPU::SaveExtendedState(uint8_t* buff)
    {
        if (buff == nullptr || !extendedState.setup)
            return;

        if (extendedState.xSaveAvailable)
        {}
        else
            asm volatile("fxsave %0" :: "m"(buff));
    }

    void CPU::LoadExtendedState(uint8_t* buff)
    {
        if (buff == nullptr || !extendedState.setup)
            return;
        
        if (extendedState.xSaveAvailable)
        {}
        else
            asm volatile("fxrstor %0" :: "m"(buff));
    }

    bool CPU::FeatureSupported(CpuFeature feature)
    {
        switch (feature)
        {
        case CpuFeature::ExecuteDisable:
            return (extLeaf1Edx & (1 << 20)) != 0;
        case CpuFeature::GigabytePages:
            return (extLeaf1Edx & (1 << 26)) != 0;
        case CpuFeature::GlobalPages:
            return (extLeaf1Edx & (1 << 13)) != 0;
        case CpuFeature::SMAP:
            return (leaf7_0Ebx & (1 << 20)) != 0;
        case CpuFeature::SMEP:
            return (leaf7_0Ebx & (1 << 7)) != 0;
        case CpuFeature::UMIP:
            return (leaf7_0Ecx & (1 << 2)) != 0;
        case CpuFeature::APIC:
            return (leaf1Edx & (1 << 9)) != 0;
        case CpuFeature::X2APIC:
            return (leaf1Ecx & (1 << 21)) != 0;

        case CpuFeature::FXSave:
            return (leaf1Edx & (1 << 24)) != 0;
        case CpuFeature::XSave:
            return (leaf1Ecx & (1 << 26)) != 0;

        case CpuFeature::FPU:
            return (leaf1Edx & (1 << 0)) != 0;
        case CpuFeature::SSE:
            return (leaf1Edx & (1 << 25)) != 0;
        case CpuFeature::SSE2:
            return (leaf1Edx & (1 << 26)) != 0;
        case CpuFeature::SSE3:
            return (leaf1Ecx & (1 << 0)) != 0;
        case CpuFeature::S_SSE3:
            return (leaf1Ecx & (1 << 9)) != 0;
        case CpuFeature::SSE4_1:
            return (leaf1Ecx & (1 << 19)) != 0;
        case CpuFeature::SSE4_2:
            return (leaf1Ecx & (1 << 20)) != 0;
        case CpuFeature::SSE4A:
            return (leaf1Ecx & (1 << 6)) != 0;
        case CpuFeature::AVX:
            return (leaf1Ecx & (1 << 28)) != 0;

        default:
            return false;
        }
    }

    const char* cpuFeatureNamesShort[] = 
    {
        "NX",
        "GigPages",
        "PGE",
        "SMAP",
        "SMEP",
        "UMIP",
        "APIC",
        "X2APIC",

        "FXSave",
        "XSave",

        "FPU",
        "SSE",
        "SSE2",
        "SSE3",
        "S_SSE3",
        "SSE4.1",
        "SSE4.2",
        "SSE4a",
        "AVX",

        "(())"
    };

    const char* cpuFeatureNamesLong[] =
    {
        "No Execute/Execute Disable",
        "1 Gigabyte Pages",
        "Global Pages",
        "Supervisor Access Prevention",
        "Supervisor Execution Prevention",
        "User Mode Instruction Prevention",
        "Advanced PIC",
        "Extended v2 APIC",

        "FXSAVE/FXRSTOR support",
        "XSAVE/XRSTOR support",

        "X87 Floating point unit",
        "Streaming SIMD extensions",
        "SSE v2",
        "SSE v3",
        "Supplemental SSE v3",
        "SSE v4, part 1",
        "SSE v4, part 2",
        "SSE v4, part 3",
        "Advanced Vector Extensions",

        "((Unknown Feature))"
    };

    const char* CPU::GetFeatureStr(CpuFeature feature, bool getFullname)
    {
        if ((unsigned)feature > (unsigned)CpuFeature::EnumCount)
            feature = CpuFeature::EnumCount;

        return getFullname ? cpuFeatureNamesLong[(unsigned)feature] : cpuFeatureNamesShort[(unsigned)feature];
    }

    void CPU::PrintInfo()
    {
        uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
        const uint32_t maxBasicLeaf = __get_cpuid_max(0, nullptr);
        const uint32_t maxExtendedLeaf = __get_cpuid_max(0x8000'0000, nullptr);

        __get_cpuid(0, &eax, &ebx, &ecx, &edx);
        char vendorStr[13];
        for (size_t i = 0; i < 4; i++)
        {
            const size_t shiftor = i * 8;
            vendorStr[0 + i] = (ebx >> shiftor) & 0xFF;
            vendorStr[4 + i] = (edx >> shiftor) & 0xFF;
            vendorStr[8 + i] = (ecx >> shiftor) & 0xFF;
        }
        vendorStr[12] = 0;

        Logf("CPUID: %u basic leaves, %u extended leaves, vendor string=%s", LogSeverity::Verbose, 
            maxBasicLeaf, maxExtendedLeaf - 0x8000'0000, vendorStr);

        if (maxExtendedLeaf >= 0x8000'0004)
        {
            char brandStr[48];
            sl::memset(brandStr, 0, 48);
            for (size_t reg = 0; reg < 3; reg++)
            {
                __get_cpuid(0x8000'0002 + reg, &eax, &ebx, &ecx, &edx);
                const size_t offset = reg * 16;

                for (size_t i = 0; i < 4; i++)
                {
                    const size_t shiftor = i * 8;
                    brandStr[offset + 0 + i] = (eax >> shiftor) & 0xFF;
                    brandStr[offset + 4 + i] = (ebx >> shiftor) & 0xFF;
                    brandStr[offset + 8 + i] = (ecx >> shiftor) & 0xFF;
                    brandStr[offset + 12 + i] = (edx >> shiftor) & 0xFF;
                }
            }
            Logf("CPUID: brand string=%s", LogSeverity::Verbose, brandStr);
        }

        for (size_t i = 0; i <= maxBasicLeaf; i++)
        {
            //serializing instruction inside a loop for extra performance.
            const int valid = __get_cpuid(i, &eax, &ebx, &ecx, &edx);
            if (!valid)
                Logf("CPUID leaf %u: invalid.", LogSeverity::Verbose, i);
            else
                Logf("CPUID leaf %u: eax=0x%0x, ebx=0x%0x, ecx=0x%0x, edx=0x%0x", LogSeverity::Verbose, i, eax, ebx, ecx, edx);
        }

        for (size_t i = 0x8000'0000; i <= maxExtendedLeaf; i++)
        {
            const int valid = __get_cpuid(i, &eax, &ebx, &ecx, &edx);
            if (!valid)
                Logf("CPUID extended leaf %u: invalid.", LogSeverity::Verbose, i - 0x8000'0000);
            else
                Logf("CPUID extended leaf %u: eax=0x%0x, ebx=0x%0x, ecx=0x%0x, edx=0x%0x", LogSeverity::Verbose, i - 0x8000'0000, eax, ebx, ecx, edx);
        }

        for (size_t i = 0; i < (size_t)CpuFeature::EnumCount; i += 4)
        {
            Logf("CPU features: %s supported=%B, %s supported=%B, %s supported=%B, %s supported=%B", LogSeverity::Verbose,
                GetFeatureStr((CpuFeature)i), FeatureSupported((CpuFeature)i), 
                GetFeatureStr((CpuFeature)(i + 1)), FeatureSupported((CpuFeature)(i + 1)),
                GetFeatureStr((CpuFeature)(i + 2)), FeatureSupported((CpuFeature)(i + 2)),
                GetFeatureStr((CpuFeature)(i + 3)), FeatureSupported((CpuFeature)(i + 3)));
        }

        Log("End of CPU info.", LogSeverity::Verbose);
    }

    void CPU::AllowSumac(bool allowed)
    {
        if (allowed)
            asm volatile("stac" ::: "cc");
        else
            asm volatile("clac" ::: "cc");
    }

    bool CPU::SumacAllowed()
    {
        uint64_t rflags;
        asm volatile("pushf; pop %0" : "=r"(rflags));
        return (rflags & (1 << 18)) != 0;
    }
}
