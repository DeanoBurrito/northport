#include <hardware/x86_64/LocalApic.hpp>
#include <hardware/x86_64/PortIo.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <hardware/x86_64/Msr.hpp>
#include <AcpiTypes.hpp>
#include <hardware/Entry.hpp>
#include <KernelApi.hpp>
#include <Mmio.h>

namespace Npk
{
    constexpr uint32_t LvtModeNmi = 1 << 10;
    constexpr uint32_t LvtMasked = 1 << 16;
    constexpr uint32_t LvtActiveLow = 1 << 13;
    constexpr uint32_t LvtLevelTrigger = 1 << 14;
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
        IcrLow = 0x300,
        IcrHigh = 0x310,
        LvtTimer = 0x320,
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
        uint64_t tscExpiry;
        uint64_t tscFreq;
        uint64_t timerFreq;
        uint32_t acpiId;
        bool x2Mode;
        bool hasTscDeadline;

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
    uintptr_t lapicMmioBase;

    static void PrepareLocalApic()
    {
        NPK_ASSERT(CpuHasFeature(CpuFeature::Apic));
        NPK_ASSERT(CpuHasFeature(CpuFeature::Tsc));

        const uint64_t baseMsr = ReadMsr(Msr::ApicBase);
        NPK_ASSERT(baseMsr & (1 << 11)); //check lapic hasnt been disabled

        lapic->x2Mode = CpuHasFeature(CpuFeature::ApicX2);
        if (lapic->x2Mode)
            WriteMsr(Msr::ApicBase, ReadMsr(Msr::ApicBase) | (1 << 10));

        if (CpuHasFeature(CpuFeature::TscDeadline))
        {
            lapic->hasTscDeadline = true;
            WriteMsr(Msr::TscDeadline, 0);
        }
    }

    static void FinishLapicInit(Madt* madt)
    {
        lapic->Write(LApicReg::SpuriousVector, LapicSpuriousVector);
        lapic->Write(LApicReg::LvtTimer, LvtMasked | LapicSpuriousVector);
        lapic->Write(LApicReg::LvtLint0, LvtMasked | LapicSpuriousVector);
        lapic->Write(LApicReg::LvtLint1, LvtMasked | LapicSpuriousVector);
        lapic->Write(LApicReg::LvtError, LvtMasked | LapicSpuriousVector);
        lapic->Write(LApicReg::TimerInitCount, 0);

        SetMyIpiId(reinterpret_cast<void*>(MyLapicId()));

        if (madt == nullptr)
            return;

        const uint32_t myLapicId = lapic->Read(LApicReg::Id) 
            >> (lapic->x2Mode ? 24 : 0);

        //first pass: find the acpi processor id assocaited with this lapic
        lapic->acpiId = -1;
        for (auto source = NextMadtSubtable(madt); source != nullptr; source = NextMadtSubtable(madt, source))
        {
            if (source->type == MadtSourceType::LocalApic)
            {
                auto src = static_cast<const MadtSources::LocalApic*>(source);
                if (src->apicId == myLapicId)
                    lapic->acpiId = src->acpiProcessorId;
            }
            else if (source->type == MadtSourceType::LocalX2Apic)
            {
                auto src = static_cast<const MadtSources::LocalX2Apic*>(source);
                if (src->apicId == myLapicId)
                    lapic->acpiId = src->acpiProcessorId;
            }
        }
        if (lapic->acpiId == (uint32_t)-1)
        {
            Log("LAPIC %u has no entry in MADT, cannot determine acpi processor id.",
                LogLevel::Error, myLapicId);
            return;
        }

        //second pass: find any nmi entries that apply to this lapic
        for (auto source = NextMadtSubtable(madt); source != nullptr; source = NextMadtSubtable(madt, source))
        {
            uint32_t targetAcpiId;
            uint16_t polarityModeFlags;
            uint8_t inputNumber;

            if (source->type == MadtSourceType::LocalApicNmi)
            {
                const auto nmi = static_cast<const MadtSources::LocalApicNmi*>(source);

                targetAcpiId = nmi->acpiProcessorId;
                if (targetAcpiId == 0xFF)
                    targetAcpiId = 0xFFFF'FFFF;
                polarityModeFlags = nmi->polarityModeFlags;
                inputNumber = nmi->lintNumber;
            }
            else if (source->type == MadtSourceType::LocalX2ApicNmi)
            {
                auto nmi = static_cast<const MadtSources::LocalX2ApicNmi*>(source);

                targetAcpiId = nmi->acpiProcessorId;
                polarityModeFlags = nmi->polarityModeFlags;
                inputNumber = nmi->lintNumber;
            }
            else
                continue;

            //0xFFFF'FFFF (and 0xFF for non-x2 apics) is a special ID meaning 'everyone'
            if (targetAcpiId != 0xFFFF'FFFF && targetAcpiId != lapic->acpiId)
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

    static void EnableLocalApic()
    {
        lapic->Write(LApicReg::SpuriousVector, LapicSpuriousVector | (1 << 8));

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
    }

    void InitBspLapic(uintptr_t& virtBase)
    {
        PrepareLocalApic();

        if (!lapic->x2Mode)
        {
            lapicMmioBase = virtBase;
            const size_t cpuCount = MyMemoryDomain().smpControls.Size(); //TODO: account for multiple domains
            virtBase += PageSize() * cpuCount;
            Log("Reserved address space for %zu LAPICs", LogLevel::Trace, cpuCount);

            lapic->mmio = lapicMmioBase;
            const Paddr mmioAddr = ReadMsr(Msr::ApicBase) & ~0xFFFul;
            ArchAddMap(MyKernelMap(), lapic->mmio.BaseAddress(), mmioAddr, MmuFlag::Write | MmuFlag::Mmio);
            Log("LAPIC registers mapped at %p", LogLevel::Verbose, lapic->mmio.BasePointer());
        }

        auto maybeMadt = GetAcpiTable(SigMadt);
        Madt* madt = maybeMadt.HasValue() ? static_cast<Madt*>(*maybeMadt) : nullptr;
        FinishLapicInit(madt);

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

        EnableLocalApic();
        Log("BSP local APIC initialized.", LogLevel::Verbose);
    }

    void InitApLapic()
    {
        PrepareLocalApic();

        if (!lapic->x2Mode)
        {
            NPK_ASSERT(lapicMmioBase != 0);

            lapic->mmio = lapicMmioBase + PageSize() * MyCoreId();
            const Paddr mmioAddr = ReadMsr(Msr::ApicBase) & ~0xFFFul;
            ArchAddMap(MyKernelMap(), lapic->mmio.BaseAddress(), mmioAddr, MmuFlag::Write | MmuFlag::Mmio);
            Log("LAPIC registers mapped at %p", LogLevel::Verbose, lapic->mmio.BasePointer());
        }

        auto maybeMadt = GetAcpiTable(SigMadt);
        Madt* madt = maybeMadt.HasValue() ? static_cast<Madt*>(*maybeMadt) : nullptr;
        FinishLapicInit(madt);

        EnableLocalApic();
        Log("AP local APIC initialized.", LogLevel::Verbose);
    }

    void SignalEoi()
    {
        lapic->Write(LApicReg::Eoi, 0);
    }

    uint32_t MyLapicId()
    {
        return lapic->Read(LApicReg::Id) >> (lapic->x2Mode ? 0 : 24);
    }

    uint8_t MyLapicVersion()
    {
        return lapic->Read(LApicReg::Version) & 0xFF;
    }

    void ArmTscInterrupt(uint64_t expiry)
    {
        const bool restoreIntrs = IntrsOff();
        if (lapic->hasTscDeadline)
        {
            lapic->Write(LApicReg::LvtTimer, (0b10 << 17) | LapicTimerVector);
            WriteMsr(Msr::TscDeadline, expiry);
        }
        else
        {
            NPK_UNREACHABLE();
            //TODO: convert tsc ticks to lapic ticks, store in tscExpiry and chain lapic interrupts
        }

        if (restoreIntrs)
            IntrsOn();
    }

    void HandleLapicTimerInterrupt()
    {
        if (lapic->hasTscDeadline)
            return DispatchAlarm();

        //we're emulating the tsc, check if we dispatch the alarm now
        if (ReadTsc() >= lapic->tscExpiry)
            return DispatchAlarm();

        //need to wait a bit longer, re-arm lapic timer
        NPK_UNREACHABLE();
    }

    void HandleLapicErrorInterrupt()
    {
        const uint32_t status = lapic->Read(LApicReg::ErrorStatus);

        Log("Local APIC error: lapic-%u%s, status=0x%x", LogLevel::Error, 
            MyLapicId(), lapic->x2Mode ? ", x2-mode" : "", status);
        lapic->Write(LApicReg::ErrorStatus, 0);
    }

    void SendIpi(uint32_t dest, IpiType type, uint8_t vector)
    {
        constexpr uint32_t LevelAssert = 1 << 14;
        constexpr uint32_t LevelTriggered = 1 << 15;

        uint32_t low;
        switch (type)
        {
        case IpiType::Init:
            low = LevelTriggered | LevelAssert | ((uint32_t)IpiType::Init << 8);
            break;
        case IpiType::InitDeAssert:
            low = LevelTriggered | ((uint32_t)IpiType::Init << 8);
            break;
        default:
            low = LevelAssert | ((uint32_t)type << 8) | vector;
            break;
        }

        lapic->Write(LApicReg::ErrorStatus, 0);

        if (lapic->x2Mode)
            WriteMsr(lapic->RegToMsr(LApicReg::IcrLow), ((uint64_t)dest << 32) | low);
        else
        {
            //IPIs are sent upon writing to IcrLow, so set the destination first
            lapic->Write(LApicReg::IcrHigh, dest << 24);
            lapic->Write(LApicReg::IcrLow, low);
        }
    }

    bool LastIpiSent()
    {
        constexpr uint32_t DeliveryPending = 1 << 12;
        constexpr uint32_t IpiFailedBits = (1 << 2) | (1 << 5);

        while (lapic->Read(LApicReg::IcrLow) & DeliveryPending)
            sl::HintSpinloop();

        return !(lapic->Read(LApicReg::ErrorStatus) & IpiFailedBits);
    }
}
