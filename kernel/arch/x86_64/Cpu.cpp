#include <arch/Cpu.h>
#include <cpuid.h>
#include <Platform.h>
#include <Log.h>

namespace Kernel
{
    char cpuidVendorString[13];
    
    uint32_t leaf1Edx;
    uint32_t leaf1Ecx;

    uint32_t leaf7_0Ebx;
    uint32_t leaf7_0Ecx;

    //tsc, core crystal in Hz. Bus, core max, core base in MHz
    uint32_t leaf15Eax;
    uint32_t leaf15Ebx;
    uint32_t leaf15Ecx;
    uint32_t leaf16Eax;
    uint32_t leaf16Ebx;
    uint32_t leaf16Ecx;

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
        
        uint64_t highestExtLeafAvailable = __get_cpuid_max(0x8000'0000, (unsigned int*)cpuidVendorString);
        uint64_t highestBaseLeafAvailable = __get_cpuid_max(0, (unsigned int*)cpuidVendorString);

        if (highestExtLeafAvailable == 0 || highestBaseLeafAvailable == 0)
        {
            cpuidVendorString[0] = 0;
            return;
        }

        //dummy regs for values we dont care about saving (or getting cpuid string)
        uint32_t eax, edx, ecx, ebx;
        
        //get vendor name
        __get_cpuid(0, &eax, &ebx, &ecx, &edx);
        cpuidVendorString[0] = ebx & 0x00'00'00'FF;
        cpuidVendorString[1] = (ebx & 0x00'00'FF'00) >> 8;
        cpuidVendorString[2] = (ebx & 0x00'FF'00'00) >> 16;
        cpuidVendorString[3] = (ebx & 0xFF'00'00'00) >> 24;
        cpuidVendorString[4] = edx & 0x00'00'00'FF;
        cpuidVendorString[5] = (edx & 0x00'00'FF'00) >> 8;
        cpuidVendorString[6] = (edx & 0x00'FF'00'00) >> 16;
        cpuidVendorString[7] = (edx & 0xFF'00'00'00) >> 24;
        cpuidVendorString[8] = ecx & 0x00'00'00'FF;
        cpuidVendorString[9] = (ecx & 0x00'00'FF'00) >> 8;
        cpuidVendorString[10] = (ecx & 0x00'FF'00'00) >> 16;
        cpuidVendorString[11] = (ecx & 0xFF'00'00'00) >> 24;
        cpuidVendorString[12] = 0; //the all important, null terminator.

        //general purpose leaves
        __get_cpuid(0x8000'0001, &eax, &ebx, &extLeaf1Ecx, &extLeaf1Edx);
        __get_cpuid(1, &eax, &ebx, &leaf1Ecx, &leaf1Edx);

        if (highestBaseLeafAvailable >= 7)
            __get_cpuid_count(7, 0, &eax, &leaf7_0Ebx, &leaf7_0Ecx, &edx);
        else
            leaf7_0Ebx = 0;

        //tsc frequency (= ecx * ebx/eax) and core crystal clock frequency in hertz (ecx)
        if (highestBaseLeafAvailable >= 0x15)
            __get_cpuid(0x15, &leaf15Eax, &leaf15Ebx, &leaf15Ecx, &edx);
        else
            leaf15Eax = leaf15Ebx = leaf15Ecx = (uint32_t)-1;
        if (leaf15Eax == 0)
            leaf15Eax = 1; //virtualbox can sometimes return 0 for some reason. Can lead to a div by zero if we're not careful.
        
        //all in MHz: core base = eax, core max = ebx, bus reference = ecx
        if (highestBaseLeafAvailable >= 0x16)
            __get_cpuid(0x15, &leaf16Eax, &leaf16Ebx, &leaf16Ecx, &edx);
        else
            leaf16Eax = leaf16Ebx = leaf16Ecx = (uint32_t)-1;
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

    const char* CPU::GetVendorString()
    {
        return cpuidVendorString;
    }

    const CpuFrequencies CPU::GetFrequencies()
    {
        CpuFrequencies freqs;
        freqs.coreClockBaseHertz = leaf16Eax;
        freqs.coreMaxBaseHertz = leaf16Ebx;
        freqs.busClockHertz = leaf16Ecx;

        if (leaf15Eax == (uint32_t)-1 || leaf15Ebx == (uint32_t)-1 || leaf15Ecx == (uint32_t)-1)
            freqs.coreTimerHertz = -1;
        else //no need to do expensive divide work here if we dont have to
            freqs.coreTimerHertz = leaf15Ecx * leaf15Ebx / leaf15Eax;

        //RVO should make this fine
        return freqs;
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
