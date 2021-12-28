#include <Cpu.h>
#include <cpuid.h>
#include <Platform.h>
#include <Log.h>

namespace Kernel
{
    char cpuidVendorString[13];
    
    uint32_t leaf1Edx;
    uint32_t leaf1Ecx;

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

    void CPU::SetInterruptsFlag(bool state)
    {
        if (state)
            asm volatile("sti");
        else
            asm volatile("cli");
    }

    void CPU::ClearInterruptsFlag()
    {
        asm volatile("cli");
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
        __get_cpuid(0x8000'0001, &eax, &ebx, &leaf1Ecx, &leaf1Edx);
        __get_cpuid(1, &eax, &ebx, &leaf1Ecx, &leaf1Edx);

        //tsc frequency (= ecx * ebx/eax) and core crystal clock frequency in hertz (ecx)
        if (highestBaseLeafAvailable >= 0x15)
            __get_cpuid(0x15, &leaf15Eax, &leaf15Ebx, &leaf15Ecx, &edx);
        else
            leaf15Eax = leaf15Ebx = leaf15Ecx = (uint32_t)-1;
        
        //all in MHz: core base = eax, core max = ebx, bus reference = ecx
        if (highestBaseLeafAvailable >= 0x16)
            __get_cpuid(0x15, &leaf16Eax, &leaf16Ebx, &leaf16Ecx, &edx);
        else
            leaf16Eax = leaf16Ebx = leaf16Ecx = (uint32_t)-1;
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

        Logf("Extended cpu state setup for core %lu", LogSeverity::Verbose, GetCoreLocal()->apicId);
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

    void CPU::PortWrite8(uint16_t port, uint8_t data)
    {
        asm volatile("outb %0, %1" :: "a"(data), "Nd"(port));
    }

    void CPU::PortWrite16(uint16_t port, uint16_t data)
    {
        asm volatile("outw %0, %1" :: "a"(data), "Nd"(port));
    }

    void CPU::PortWrite32(uint16_t port, uint32_t data)
    {
        asm volatile("outl %0, %1" :: "a"(data), "Nd"(port));
    }

    uint8_t CPU::PortRead8(uint16_t port)
    {
        uint8_t value;
        asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    }

    uint16_t CPU::PortRead16(uint16_t port)
    {
        uint16_t value;
        asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    }

    uint32_t CPU::PortRead32(uint16_t port)
    {
        uint32_t value;
        asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    }

    bool CPU::FeatureSupported(CpuFeature feature)
    {
        switch (feature)
        {
        case CpuFeature::ExecuteDisable:
            return (extLeaf1Edx & (1 << 20)) != 0;
        case CpuFeature::GigabytePages:
            return (extLeaf1Edx & (1 << 26)) != 0;
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

    void CPU::WriteMsr(uint32_t address, uint64_t data)
    {
        asm volatile("wrmsr" :: "a"((uint32_t)data), "d"(data >> 32), "c"(address));
    }

    uint64_t CPU::ReadMsr(uint32_t address)
    {
        uint32_t high, low;
        asm volatile("rdmsr": "=a"(low), "=d"(high) : "c"(address));
        return ((uint64_t)high << 32) | low;
    }

    const char* cpuFeatureNamesShort[] = 
    {
        "NX",
        "GigPages",
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
}
