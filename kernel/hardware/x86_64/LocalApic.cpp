#include <hardware/x86_64/LocalApic.hpp>
#include <hardware/x86_64/PortIo.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <hardware/x86_64/Msr.hpp>
#include <AcpiTypes.hpp>
#include <KernelApi.hpp>
#include <Mmio.h>

namespace Npk
{
    constexpr uint32_t LvtModeNmi = 1 << 10;
    constexpr uint32_t LvtMasked = 1 << 16;
    constexpr uint32_t LvtActiveLow = 1 << 13;
    constexpr uint32_t LvtLevelTrigger = 1 << 15;
    constexpr uint8_t SpuriousVector = 0xFF;
    constexpr uint8_t PicIrqBase = 0x20;

    enum class LApicReg
    {
        Id = 0x20,
        Version = 0x30,
        Tpr = 0x80,
        Apr = 0x90,
        Ppr = 0xA0,
        Eoi = 0xB0,
        RemoteRead = 0xC0,
        LocalDestination = 0xD0,
        DestinationFormat = 0xE0,
        SpuriousVector = 0xF0,

        Isr0 = 0x100,
        Isr1 = 0x110,
        Isr2 = 0x120,
        Isr3 = 0x130,
        Isr4 = 0x140,
        Isr5 = 0x150,
        Isr6 = 0x160,
        Isr7 = 0x170,

        ErrorStatus = 0x280,
        LvtCmci = 0x2F0,
        IcrLow = 0x300,
        IcrHigh = 0x310,
        LvtTimer = 0x320,
        LvtThermalSensor = 0x330,
        LvtPerfMonitor = 0x340,
        LvtLint0 = 0x350,
        LvtLint1 = 0x360,
        LvtError = 0x370,

        TimerInitCount = 0x380,
        TimerCount = 0x390,
        TimerDivisor = 0x3E0,
    };

    struct LocalApic
    {
        sl::MmioRegisters<LApicReg, uint32_t> mmio;
        size_t tscFreq;
        size_t timerFreq;
        bool x2Mode;

        inline Msr RegToMsr(LApicReg reg)
        {
            return static_cast<Msr>((static_cast<size_t>(reg) >> 4) 
                + static_cast<size_t>(Msr::X2ApicBase));
        }

        inline uint32_t Read(LApicReg reg)
        {
            return x2Mode ? ReadMsr(RegToMsr(reg)) : mmio.Read(reg);
        }

        inline void Write(LApicReg reg, uint32_t value)
        {
            return x2Mode ? WriteMsr(RegToMsr(reg), value) : mmio.Write(reg, value);
        }
    };

    CPU_LOCAL(LocalApic, lapic);

    static void PrepareLocalApic()
    {
        NPK_ASSERT(CpuHasFeature(CpuFeature::Apic));
        NPK_ASSERT(CpuHasFeature(CpuFeature::Tsc));

        const uint64_t baseMsr = ReadMsr(Msr::ApicBase);
        NPK_ASSERT(baseMsr & (1 << 11)); //check lapic hasnt been disabled

        lapic->x2Mode = CpuHasFeature(CpuFeature::ApicX2);
        if (lapic->x2Mode)
            WriteMsr(Msr::ApicBase, ReadMsr(Msr::ApicBase) | (1 << 10));
    }

    static void FinishLapicInit(Madt* madt, uint32_t acpiId)
    {
        lapic->Write(LApicReg::LvtTimer, LvtMasked | SpuriousVector);
        lapic->Write(LApicReg::LvtThermalSensor, LvtMasked | SpuriousVector);
        lapic->Write(LApicReg::LvtPerfMonitor, LvtMasked | SpuriousVector);
        lapic->Write(LApicReg::LvtLint0, LvtMasked | SpuriousVector);
        lapic->Write(LApicReg::LvtLint1, LvtMasked | SpuriousVector);
        lapic->Write(LApicReg::LvtError, LvtMasked | SpuriousVector);

        if (madt == nullptr)
            return;

        char* scan = reinterpret_cast<char*>(madt) + sizeof(Madt);
        const char* end = scan + madt->length;
        while (scan < end)
        {
            const auto source = reinterpret_cast<MadtSource*>(scan);
            scan += source->length;

            uint32_t targetAcpiId;
            uint16_t polarityModeFlags;
            uint8_t inputNumber;

            if (source->type == MadtSourceType::LocalApicNmi)
            {
                const auto nmi = static_cast<MadtSources::LocalApicNmi*>(source);

                targetAcpiId = nmi->acpiProcessorId;
                if (targetAcpiId == 0xFF)
                    targetAcpiId = 0xFFFF'FFFF;
                polarityModeFlags = nmi->polarityModeFlags;
                inputNumber = nmi->lintNumber;
            }
            else if (source->type == MadtSourceType::LocalX2ApicNmi)
            {
                const auto nmi = static_cast<MadtSources::LocalX2ApicNmi*>(source);

                targetAcpiId = nmi->acpiProcessorId;
                polarityModeFlags = nmi->polarityModeFlags;
                inputNumber = nmi->lintNumber;
            }
            else
                continue;

            //0xFFFF'FFFF (and 0xFF for non-x2 apics) is a special ID meaning 'everyone'
            if (targetAcpiId != 0xFFFF'FFFF && targetAcpiId != acpiId)
                continue;

            const LApicReg lvt = inputNumber == 1 ? LApicReg::LvtLint1 : LApicReg::LvtLint0;
            const bool activeLow = (polarityModeFlags & MadtSources::PolarityMask) == MadtSources::PolarityLow;
            const bool levelTriggered = (polarityModeFlags & MadtSources::TriggerModeMask) == MadtSources::TriggerModeLevel;
            const uint32_t value = LvtModeNmi 
                | (activeLow ? LvtActiveLow : 0) 
                | (levelTriggered ? LvtLevelTrigger : 0);

            lapic->Write(lvt, value);
            Log("Applied lapic nmi override: lint%u, active-%s, %s-triggered.", LogLevel::Verbose,
                inputNumber, activeLow ? "low" : "high", levelTriggered ? "level" : "edge");
        }
    }

    void InitBspLapic(InitState& state)
    {
        PrepareLocalApic();

        if (!lapic->x2Mode)
        {
            const Paddr mmioAddr = ReadMsr(Msr::ApicBase) & ~0xFFFul;
            lapic->mmio = state.VmAlloc(PageSize());
            ArchEarlyMap(state, mmioAddr, lapic->mmio.BaseAddress(), MmuFlag::Write | MmuFlag::Mmio);
        }

        FinishLapicInit(nullptr, 0);

        //TODO: add detection for PICs being present (there's a bit in acpi/madt)

        //BSP should take care of initializing, remapping and masking the PICs.
        Out8(Port::Pic0Command, 0x11);
        Out8(Port::Pic1Command, 0x11);
        Out8(Port::Pic0Data, PicIrqBase);
        Out8(Port::Pic1Data, PicIrqBase + 8);
        Out8(Port::Pic0Data, 4);
        Out8(Port::Pic1Data, 2);
        Out8(Port::Pic0Data, 1);
        Out8(Port::Pic1Data, 1);
        Out8(Port::Pic0Data, 0xFF);
        Out8(Port::Pic1Data, 0xFF);

        //enable local apic
        lapic->Write(LApicReg::SpuriousVector, SpuriousVector | (1 << 8));

        //https://github.com/projectacrn/acrn-hypervisor/blob/master/hypervisor/arch/x86/lapic.c#L65
        //tl;dr: sometimes are pending interrupts left over from when the firmware or bootloader
        //was using the hardware, so we acknowledge any pending interrupts now, so the
        //kernel starts with a clean slate.
        for (size_t i = 8; i > 0; i--)
        {
            const LApicReg reg = static_cast<LApicReg>(static_cast<unsigned>(LApicReg::Isr0) 
                + (i - 1) * 0x10);
            while (lapic->Read(reg) != 0)
                SignalEoi();
        }

        Log("BSP local APIC initialized", LogLevel::Verbose);
    }

    void SignalEoi()
    {
        lapic->Write(LApicReg::Eoi, 0);
    }
}
