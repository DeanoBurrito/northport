#include <hardware/x86_64/LocalApic.hpp>
#include <hardware/x86_64/PortIo.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <hardware/x86_64/Msr.hpp>
#include <hardware/x86_64/Private.hpp>
#include <hardware/x86_64/Tsc.hpp>
#include <private/Hardware.hpp>
#include <Core.hpp>
#include <Vm.hpp>
#include <lib/AcpiTypes.hpp>
#include <lib/Maths.hpp>
#include <lib/Mmio.hpp>
#include <lib/Units.hpp>

namespace Npk
{
    constexpr uint32_t LvtModeNmi = 1 << 10;
    constexpr uint32_t LvtMasked = 1 << 16;
    constexpr uint32_t LvtActiveLow = 1 << 13;
    constexpr uint32_t LvtLevelTrigger = 1 << 14;
    constexpr uint8_t PicIrqBase = 0x20;
    constexpr uint32_t TimerDivisor1 = 0b1011;

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
        uint32_t acpiId;
        bool x2Mode;

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

    struct LapicSample
    {
        uint64_t tsc;
        uint32_t count;
        uint64_t accuracy;
    };

    CPU_LOCAL(LocalApic, lapic);
    uintptr_t lapicMmioBase;

    sl::Span<uint32_t> apicIds;

    static bool PrepareLocalApic()
    {
        NPK_CHECK(CpuHasFeature(CpuFeature::Apic), false);
        NPK_CHECK(CpuHasFeature(CpuFeature::Tsc), false);

        const uint64_t baseMsr = ReadMsr(Msr::ApicBase);
        NPK_CHECK(baseMsr & (1 << 11), false); //check lapic hasnt been disabled

        lapic->x2Mode = CpuHasFeature(CpuFeature::ApicX2);
        if (lapic->x2Mode)
            WriteMsr(Msr::ApicBase, ReadMsr(Msr::ApicBase) | (1 << 10));

        return true;
    }

    static bool FinishLapicInit(sl::Madt* madt)
    {
        using namespace sl::MadtSources;

        if (madt == nullptr)
            return false;

        lapic->Write(LApicReg::SpuriousVector, LapicSpuriousVector);
        lapic->Write(LApicReg::LvtTimer, LvtMasked | LapicSpuriousVector);
        lapic->Write(LApicReg::LvtLint0, LvtMasked | LapicSpuriousVector);
        lapic->Write(LApicReg::LvtLint1, LvtMasked | LapicSpuriousVector);
        lapic->Write(LApicReg::LvtError, LvtMasked | LapicSpuriousVector);
        lapic->Write(LApicReg::TimerInitCount, 0);

        apicIds[MyCoreId()] = MyLapicId();
        const uint32_t myLapicId = MyLapicId();

        //first pass: find the acpi processor id associated with this lapic
        lapic->acpiId = -1;
        for (auto s = sl::NextMadtSubtable(madt); s != nullptr;
            s = sl::NextMadtSubtable(madt, s))
        {
            if (s->type == sl::MadtSourceType::LocalApic)
            {
                auto src = static_cast<const sl::MadtSources::LocalApic*>(s);

                if (src->apicId == myLapicId)
                    lapic->acpiId = src->acpiProcessorId;
            }
            else if (s->type == sl::MadtSourceType::LocalX2Apic)
            {
                auto src = static_cast<const LocalX2Apic*>(s);

                if (src->apicId == myLapicId)
                    lapic->acpiId = src->acpiProcessorId;
            }
        }

        if (lapic->acpiId == (uint32_t)-1)
        {
            Log("LAPIC %u has no entry in MADT", LogLevel::Error, myLapicId);

            return false;
        }

        //second pass: find any nmi entries that apply to this lapic
        for (auto s = sl::NextMadtSubtable(madt); s != nullptr; 
            s = sl::NextMadtSubtable(madt, s))
        {
            uint32_t targetAcpiId;
            uint16_t polarityModeFlags;
            uint8_t inputNumber;

            if (s->type == sl::MadtSourceType::LocalApicNmi)
            {
                auto nmi = static_cast<const LocalApicNmi*>(s);

                targetAcpiId = nmi->acpiProcessorId;
                if (targetAcpiId == 0xFF)
                    targetAcpiId = 0xFFFF'FFFF;

                polarityModeFlags = nmi->polarityModeFlags;
                inputNumber = nmi->lintNumber;
            }
            else if (s->type == sl::MadtSourceType::LocalX2ApicNmi)
            {
                auto nmi = static_cast<const LocalX2ApicNmi*>(s);

                targetAcpiId = nmi->acpiProcessorId;
                polarityModeFlags = nmi->polarityModeFlags;
                inputNumber = nmi->lintNumber;
            }
            else
                continue;

            //0xFFFF'FFFF (and 0xFF for non-x2 apics) means 'apply to all'
            if (targetAcpiId != 0xFFFF'FFFF && targetAcpiId != lapic->acpiId)
                continue;

            LApicReg lvt = LApicReg::LvtLint0;
            if (inputNumber == 1)
                lvt = LApicReg::LvtLint1;

            const bool activeLow =
                (polarityModeFlags & PolarityMask) == PolarityLow;
            const bool levelTriggered =
                (polarityModeFlags & TriggerModeMask) == TriggerModeLevel;
            const uint32_t value = LvtModeNmi 
                | (activeLow ? LvtActiveLow : 0) 
                | (levelTriggered ? LvtLevelTrigger : 0);

            lapic->Write(lvt, value);
            Log("Applied lapic nmi override: lint%u, active-%s, %s-triggered.", 
                LogLevel::Verbose, inputNumber, activeLow ? "low" : "high", 
                levelTriggered ? "level" : "edge");
        }

        return true;
    }

    static void EnableLocalApic()
    {
        lapic->Write(LApicReg::SpuriousVector, LapicSpuriousVector | (1 << 8));

        //https://github.com/projectacrn/acrn-hypervisor/blob/master/
        //hypervisor/arch/x86/lapic.c#L65
        //
        //the issue here is sometimes there are pending interrupts from earlier
        //in the system's life (e.g. when firmware was running or before a
        //reset), so we keep signalling EOI until those are all handled,
        //so the kernel begins with the assumed clean slate.

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
            Log("Reserved address space for %zu LAPICs", LogLevel::Trace, 
                cpuCount);

            lapic->mmio = lapicMmioBase;
            const Paddr mmioAddr = ReadMsr(Msr::ApicBase) & ~0xFFFul;
            SetKernelMap(lapic->mmio.BaseAddress(), mmioAddr, 
                VmFlag::Write | VmFlag::Mmio);
            Log("LAPIC registers mapped at %p", LogLevel::Verbose,
                lapic->mmio.BasePointer());
        }

        auto maybeMadt = GetAcpiTable(sl::SigMadt);
        NPK_CHECK(maybeMadt.HasValue() && *maybeMadt != nullptr, false);
        auto madt = static_cast<sl::Madt*>(*maybeMadt);

        if (!FinishLapicInit(madt))
            return false;

        if (madt != nullptr && madt->flags.Has(sl::MadtFlag::PcAtCompat))
        {
            //BSP takes responsiblity for masking and rebasing the PICs

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
        
            Log("Legacy PICs rebased and masked.", LogLevel::Verbose);
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

            Log("LAPIC registers mapped at %p", LogLevel::Verbose, 
                lapic->mmio.BasePointer());
        }

        auto maybeMadt = GetAcpiTable(sl::SigMadt);
        NPK_CHECK(maybeMadt.HasValue() && *maybeMadt != nullptr, false);
        auto madt = static_cast<sl::Madt*>(*maybeMadt);

        if (!FinishLapicInit(madt))
            return false;

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

    static LapicSample SampleLapicTimer()
    {
        const uint64_t t0 = ReadTsc();
        const uint32_t count = lapic->Read(LApicReg::TimerCount);
        const uint64_t t1 = ReadTsc();

        LapicSample sample {};
        sample.tsc = t0 + ((t1 - t0) / 2);
        sample.count = count;
        sample.accuracy = t1 - t0;

        return sample;
    }

    sl::TimeConversion CalibrateLapicTimer()
    {
        constexpr uint32_t BeginCount = 0xFFFF'FFFF;

        const size_t runs = ReadConfigUint("npk.x86.lapic_calib_runs", 5);
        const uint64_t sampleWindow = TscFrequency() 
            / ReadConfigUint("npk.x86.lapic_calib_hz", 1000);

        lapic->Write(LApicReg::TimerDivisor, TimerDivisor1);
        lapic->Write(LApicReg::LvtTimer, LvtMasked | LapicSpuriousVector);

        uint64_t accuracy = ~0ull;
        uint64_t tscValue = 0;
        uint64_t lapicValue = 0;

        for (size_t i = 0; i < runs; i++)
        {
            lapic->Write(LApicReg::TimerInitCount, BeginCount);

            const auto begin = SampleLapicTimer();
            while (ReadTsc() - begin.tsc < sampleWindow)
                sl::HintSpinloop();
            const auto end = SampleLapicTimer();

            lapic->Write(LApicReg::TimerInitCount, 0);
            if (end.count == 0 || end.count >= begin.count)
                continue;

            const uint64_t bracket = begin.accuracy + end.accuracy;
            if (bracket >= accuracy)
                continue;

            accuracy = bracket;
            tscValue = end.tsc - begin.tsc;
            lapicValue = begin.count - end.count;
        }

        NPK_ASSERT(tscValue != 0 && lapicValue != 0);

        const auto lapicHz = lapicValue * TscFrequency() / tscValue;
        const auto conv = sl::ConvertUnits(lapicHz, sl::UnitBase::Decimal);
        Log("LAPIC timer is %zuHz (%zu.%zu %sHz), +/- %zuppm",
            LogLevel::Info, lapicHz, conv.major, conv.minor, conv.prefix,
            accuracy);

        const auto ratio = sl::TimeConversion::Create(tscValue, lapicValue);

        return ratio;
    }

    void SetupLapicTimer(bool useTscDeadline)
    {
        if (useTscDeadline)
        {
            lapic->Write(LApicReg::LvtTimer, LapicTimerVector | (1 << 18));
            asm volatile("mfence" ::: "memory");
        }
        else
        {
            lapic->Write(LApicReg::TimerDivisor, TimerDivisor1);
            lapic->Write(LApicReg::LvtTimer, LapicTimerVector);
        }
    }

    void ArmLapicTimer(uint32_t ticks)
    {
        lapic->Write(LApicReg::TimerInitCount, ticks);
    }

    void DisarmLapicTimer()
    {
        lapic->Write(LApicReg::TimerInitCount, 0);
    }

    void HandleLapicErrorInterrupt()
    {
        const uint32_t status = lapic->Read(LApicReg::ErrorStatus);
        lapic->Write(LApicReg::ErrorStatus, 0);

        Log("Local APIC error: lapic-%u%s, status=0x%x", LogLevel::Error, 
            MyLapicId(), lapic->x2Mode ? ", x2-mode" : "", status);
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
