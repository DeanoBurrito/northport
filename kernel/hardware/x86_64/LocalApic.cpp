#include <hardware/x86_64/LocalApic.hpp>
#include <hardware/x86_64/PortIo.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <hardware/x86_64/Msr.hpp>
#include <hardware/x86_64/Tsc.hpp>
#include <HardwarePrivate.hpp>
#include <AcpiTypes.hpp>
#include <Core.hpp>
#include <Vm.hpp>
#include <Maths.hpp>
#include <Mmio.hpp>
#include <UnitConverter.hpp>

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
            if (x2Mode) 
                return ReadMsr(RegToMsr(reg));
            else
                return mmio.Read(reg);
        }

        inline void Write(LApicReg reg, uint32_t value)
        {
            if (x2Mode)
                WriteMsr(RegToMsr(reg), value);
            else
                mmio.Write(reg, value);
        }
    };

    CPU_LOCAL(LocalApic, lapic);
    uintptr_t lapicMmioBase;

    static bool PrepareLocalApic()
    {
        NPK_CHECK(CpuHasFeature(CpuFeature::Apic), false);
        NPK_CHECK(CpuHasFeature(CpuFeature::Tsc), false);

        const uint64_t baseMsr = ReadMsr(Msr::ApicBase);
        NPK_CHECK(baseMsr & (1 << 11), false); //check lapic hasnt been disabled

        lapic->x2Mode = CpuHasFeature(CpuFeature::ApicX2);
        if (lapic->x2Mode)
            WriteMsr(Msr::ApicBase, ReadMsr(Msr::ApicBase) | (1 << 10));

        if (CpuHasFeature(CpuFeature::TscDeadline))
        {
            lapic->hasTscDeadline = true;
            WriteMsr(Msr::TscDeadline, 0);
        }

        return true;
    }

    static bool CalibrateLapicTimer()
    {
        //this follows a similar pattern to calibrating the tsc, see that
        //file for more thoughts.
        if (auto freq = ReadConfigUint("npk.x86.lapic_freq_override", 0); freq != 0)
        {
            Log("LAPIC timer frequency set to %zuHz by command line override",
                LogLevel::Trace, freq);
            lapic->timerFreq = freq;
            return true;
        }

        CpuidLeaf cpuid {};
        const size_t baseLeaves = DoCpuid(BaseLeaf, 0, cpuid).a;

        DoCpuid(0x15, 0, cpuid);
        if (baseLeaves > 0x15 && cpuid.c != 0)
        {
            Log("LAPIC timer frequency acquired from cpuid 0x15: %uHz",
                LogLevel::Trace, cpuid.c);
            lapic->timerFreq = cpuid.c;
            return true;
        }

        DoCpuid(0x16, 0, cpuid);
        if (baseLeaves > 0x16 && cpuid.c != 0)
        {
            Log("LAPIC timer frequency acquired from cpuid 0x16: %uMHz",
                LogLevel::Trace, cpuid.c);
            lapic->timerFreq = cpuid.c;
            return true;
        }

        DoCpuid(HypervisorLeaf, 0, cpuid);
        if (cpuid.a >= 0x10 && DoCpuid(HypervisorLeaf + 0x10, 0, cpuid).b != 0)
        {
            Log("LAPIC timer acquired from cpuid 0x4000'1000: %uKHz",
                LogLevel::Trace, cpuid.b);
            lapic->timerFreq = cpuid.b * 1000;
            return true;
        }

        constexpr size_t MaxCalibRuns = 64;
        const size_t calibRuns = sl::Clamp<size_t>(
            ReadConfigUint("npk.x86.lapic_calibration_runs", 10), 1, MaxCalibRuns);
        const size_t sampleFreq = sl::Clamp<size_t>(
            ReadConfigUint("npk.x86.lapic_sample_freq", 100), 10, 1000);
        const size_t neededRuns = sl::Clamp<size_t>(
            ReadConfigUint("npk.x86.lapic_needed_runs", 7), 1, calibRuns);
        const size_t controlRuns = sl::Clamp<size_t>(
            ReadConfigUint("npk.x86.lapic_control_runs", 5), 1, calibRuns);
        const bool dumpCalibData = 
            ReadConfigUint("npk.x86.lapic_dump_calibration", true);

        size_t controlOffset = 0;
        uint64_t calibData[MaxCalibRuns];
        auto calibNanos = sl::TimeCount(sampleFreq, 1).Rebase(sl::Nanos).ticks;
        Log("Calibrating LAPIC timer: sampling=%zu hz, runs=%zu (mulligans=%zu,"
            "control=%zu)", LogLevel::Trace, sampleFreq, calibRuns,
            calibRuns - neededRuns, controlRuns);

        lapic->Write(LApicReg::LvtTimer, (0b00 << 17) | LvtMasked);
        lapic->Write(LApicReg::TimerDivisor, 0);

        for (size_t i = 0; i < controlRuns; i++)
        {
            constexpr uint32_t BeginValue = 0xFFFF'FFFF;

            AcquireReferenceTimerLock();
            lapic->Write(LApicReg::TimerInitCount, BeginValue);
            ReferenceSleep(0);
            const uint32_t endValue = lapic->Read(LApicReg::TimerCount);
            ReleaseReferenceTimerLock();

            lapic->Write(LApicReg::TimerInitCount, 0);
            controlOffset += BeginValue - endValue;
        }

        controlOffset /= controlRuns;
        Log("Control offset for reference timer: %zu lapic ticks", 
            LogLevel::Trace, controlOffset);

        for (size_t i = 0; i < calibRuns; i++)
        {
            constexpr uint32_t BeginValue = 0xFFFF'FFFF;

            AcquireReferenceTimerLock();
            lapic->Write(LApicReg::TimerInitCount, BeginValue);
            const uint64_t realCalibNanos = ReferenceSleep(calibNanos);
            const uint32_t stopValue = lapic->Read(LApicReg::TimerCount);
            ReleaseReferenceTimerLock();

            lapic->Write(LApicReg::TimerInitCount, 0);
            NPK_CHECK(stopValue != 0, false);

            if (realCalibNanos == 0)
            {
                calibData[i] = 0;
                continue;
            }

            calibData[i] = BeginValue - stopValue;
            calibData[i] = (calibData[i] * calibNanos) / realCalibNanos;
            if (dumpCalibData)
            {
                Log("LAPIC timer calibratun run: begin=%u, end=%u, adjusted=%zu"
                    ", slept=%zuns", LogLevel::Verbose, BeginValue, stopValue, 
                    calibData[i], realCalibNanos);
            }
        }

        sl::Span<uint64_t> runs { calibData, calibRuns };
        const auto maybePeriod = CoalesceTimerData(runs, calibRuns - neededRuns);
        NPK_CHECK(maybePeriod.HasValue(), false);

        const uint64_t timerFreq = *maybePeriod * sampleFreq;
        const auto conv = sl::ConvertUnits(timerFreq, sl::UnitBase::Decimal);
        Log("LAPIC timer calibrated as %zu Hz (%zu.%zu %sHz)", LogLevel::Info, 
            timerFreq, conv.major, conv.minor, conv.prefix);

        lapic->timerFreq = timerFreq;
        return true;
    }

    static void FinishLapicInit(sl::Madt* madt)
    {
        lapic->Write(LApicReg::SpuriousVector, LapicSpuriousVector);
        lapic->Write(LApicReg::LvtTimer, LvtMasked | LapicSpuriousVector);
        lapic->Write(LApicReg::LvtLint0, LvtMasked | LapicSpuriousVector);
        lapic->Write(LApicReg::LvtLint1, LvtMasked | LapicSpuriousVector);
        lapic->Write(LApicReg::LvtError, LvtMasked | LapicSpuriousVector);
        lapic->Write(LApicReg::TimerInitCount, 0);

        SetMyIpiId(reinterpret_cast<void*>(MyLapicId()));

        if (!lapic->hasTscDeadline)
            NPK_ASSERT(CalibrateLapicTimer());

        if (madt == nullptr)
            return;

        const uint32_t myLapicId = MyLapicId();

        //first pass: find the acpi processor id associated with this lapic
        lapic->acpiId = -1;
        for (auto source = sl::NextMadtSubtable(madt); source != nullptr; source = sl::NextMadtSubtable(madt, source))
        {
            if (source->type == sl::MadtSourceType::LocalApic)
            {
                auto src = static_cast<const sl::MadtSources::LocalApic*>(source);
                if (src->apicId == myLapicId)
                    lapic->acpiId = src->acpiProcessorId;
            }
            else if (source->type == sl::MadtSourceType::LocalX2Apic)
            {
                auto src = static_cast<const sl::MadtSources::LocalX2Apic*>(source);
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
        for (auto source = sl::NextMadtSubtable(madt); source != nullptr; source = sl::NextMadtSubtable(madt, source))
        {
            uint32_t targetAcpiId;
            uint16_t polarityModeFlags;
            uint8_t inputNumber;

            if (source->type == sl::MadtSourceType::LocalApicNmi)
            {
                auto nmi = static_cast<const sl::MadtSources::LocalApicNmi*>(source);

                targetAcpiId = nmi->acpiProcessorId;
                if (targetAcpiId == 0xFF)
                    targetAcpiId = 0xFFFF'FFFF;
                polarityModeFlags = nmi->polarityModeFlags;
                inputNumber = nmi->lintNumber;
            }
            else if (source->type == sl::MadtSourceType::LocalX2ApicNmi)
            {
                auto nmi = static_cast<const sl::MadtSources::LocalX2ApicNmi*>(source);

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
            const bool activeLow = (polarityModeFlags & sl::MadtSources::PolarityMask) == sl::MadtSources::PolarityLow;
            const bool levelTriggered = (polarityModeFlags & sl::MadtSources::TriggerModeMask) == sl::MadtSources::TriggerModeLevel;
            const uint32_t value = LvtModeNmi 
                | (activeLow ? LvtActiveLow : 0) 
                | (levelTriggered ? LvtLevelTrigger : 0);

            lapic->Write(lvt, value);
            Log("Applied lapic nmi override: lint%u, active-%s, %s-triggered.", 
                LogLevel::Verbose, inputNumber, activeLow ? "low" : "high", 
                levelTriggered ? "level" : "edge");
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
            const LApicReg reg = static_cast<LApicReg>(
                static_cast<unsigned>(LApicReg::Isr0) + (i - 1) * 0x10);

            while (lapic->Read(reg) != 0)
                SignalEoi();
        }
    }

    bool InitBspLapic(uintptr_t& virtBase)
    {
        if (!PrepareLocalApic())
            return false;

        if (!lapic->x2Mode)
        {
            lapicMmioBase = virtBase;
            const size_t cpuCount = MySystemDomain().smpControls.Size();
            virtBase += PageSize() * cpuCount;
            Log("Reserved address space for %zu LAPICs", LogLevel::Trace, cpuCount);

            lapic->mmio = lapicMmioBase;
            const Paddr mmioAddr = ReadMsr(Msr::ApicBase) & ~0xFFFul;
            SetKernelMap(lapic->mmio.BaseAddress(), mmioAddr, 
                VmFlag::Write | VmFlag::Mmio);
            Log("LAPIC registers mapped at %p", LogLevel::Verbose, lapic->mmio.BasePointer());
        }

        auto maybeMadt = GetAcpiTable(sl::SigMadt);
        auto madt = maybeMadt.HasValue() ? static_cast<sl::Madt*>(*maybeMadt) : nullptr;
        FinishLapicInit(madt);

        if (madt != nullptr && madt->flags.Has(sl::MadtFlag::PcAtCompat))
        {
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
        }

        EnableLocalApic();
        Log("BSP local APIC initialized.", LogLevel::Verbose);
        return true;
    }

    bool InitApLapic()
    {
        if (!PrepareLocalApic())
            return false;

        if (!lapic->x2Mode)
        {
            NPK_CHECK(lapicMmioBase != 0, false);

            lapic->mmio = lapicMmioBase + PageSize() * MyCoreId();
            const Paddr mmioAddr = ReadMsr(Msr::ApicBase) & ~0xFFFul;
            SetKernelMap(lapic->mmio.BaseAddress(), mmioAddr,
                VmFlag::Write | VmFlag::Mmio);
            Log("LAPIC registers mapped at %p", LogLevel::Verbose, lapic->mmio.BasePointer());
        }

        auto maybeMadt = GetAcpiTable(sl::SigMadt);
        auto madt = maybeMadt.HasValue() ? static_cast<sl::Madt*>(*maybeMadt) : nullptr;
        FinishLapicInit(madt);

        EnableLocalApic();
        Log("AP local APIC initialized.", LogLevel::Verbose);
        return true;
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

    static void ArmLapicTimer()
    {
        const uint64_t tscTicks = lapic->tscExpiry - ReadTsc();
        const uint64_t lapicTicks = tscTicks * lapic->timerFreq / MyTscFrequency();
        const uint32_t intrTicks = sl::Min<uint32_t>(0xFFFF'FFFF, lapicTicks);

        lapic->Write(LApicReg::LvtTimer, LapicTimerVector);
        lapic->Write(LApicReg::TimerInitCount, intrTicks);
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
            lapic->tscExpiry = expiry;
            ArmLapicTimer();
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

        ArmLapicTimer();
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
        {
            const uint64_t value = ((uint64_t)dest << 32) | low;
            WriteMsr(lapic->RegToMsr(LApicReg::IcrLow), value);
        }
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
