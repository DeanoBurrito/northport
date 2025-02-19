#include <arch/x86_64/Apic.h>
#include <arch/x86_64/Cpuid.h>
#include <arch/x86_64/Timers.h>
#include <core/Log.h>
#include <core/WiredHeap.h>
#include <core/Config.h>
#include <services/AcpiTables.h>
#include <services/Vmm.h>
#include <Locks.h>
#include <Hhdm.h>
#include <Maths.h>
#include <Memory.h>
#include <containers/List.h>
#include <UnitConverter.h>

namespace Npk
{
    constexpr size_t CalibrationRuns = 8;
    constexpr size_t ControlRuns = 5;
    constexpr size_t RequiredRuns = 5;
    constexpr size_t SampleFrequency = 100;

    //https://docs.kernel.org/virt/kvm/x86/msr.html
    struct SL_PACKED(PvSystemTime
    {
        uint32_t version;
        uint32_t reserved0;
        uint64_t tscReference;
        uint64_t systemTime;
        uint32_t tscToSystemMul;
        int8_t tscShift;
        uint8_t flags;
        uint8_t reserved1[2];
    });

    PvSystemTime* pvClock = nullptr;

    uint32_t LocalApic::ReadReg(LapicReg reg)
    {
        if (x2Mode)
            return ReadMsr((static_cast<uint32_t>(reg) >> 4) + 0x800);
        else
            return mmio.Offset(static_cast<uint32_t>(reg)).Read<uint32_t>();
    }

    void LocalApic::WriteReg(LapicReg reg, uint32_t value)
    {
        if (x2Mode)
            WriteMsr((static_cast<uint32_t>(reg) >> 4) + 0x800, value);
        else
            mmio.Offset(static_cast<uint32_t>(reg)).Write<uint32_t>(value);
    }

    static bool CoalesceTimerRuns(sl::Span<size_t> runs, size_t allowedFails, bool printInfo)
    {
        const size_t stdDev = sl::StandardDeviation(runs);
        const size_t mean = [&]() -> size_t
        {
            size_t accum = 0;
            for (size_t i = 0; i < runs.Size(); i++)
                accum += runs[i];
            return accum / runs.Size();
        }();
        
        size_t validRuns = 0;
        size_t accumulator = 0;
        for (size_t i = 0; i < runs.Size(); i++)
        {
            if (runs[i] < mean - stdDev || runs[i] > mean + stdDev)
                continue;

            validRuns++;
            accumulator += runs[i];
        }

        if (validRuns < runs.Size() - allowedFails)
            return false;

        runs[0] = accumulator / validRuns;
        if (printInfo)
            Log("%zu/%zu valid runs, %zu final ticks", LogLevel::Verbose, validRuns, runs.Size(), runs[0]);
        return true;
    }

    void LocalApic::CalibrateLocalTimer(bool dumpCalibData, size_t maxBaseCpuidLeaf, size_t maxHyperCpuidLeaf)
    {
        //we cant use the TSC for the interrupt timer source, so we'll need the frequency
        //of the lapic timer. First we try cpuid leaves 0x15 and 0x16, and then the
        //hypervisor leaf (if present), and then otherwise fallback to calibating it
        //against another timer in the system.

        timerFrequency = 0;
        size_t calibData[CalibrationRuns];
        const auto sleepTime = sl::TimeCount(SampleFrequency, 1).Rebase(sl::Nanos).ticks;

        CpuidLeaf cpuidData {};
        if (maxBaseCpuidLeaf >= 0x15 && DoCpuid(0x15, 0, cpuidData).c != 0)
        {
            timerFrequency = cpuidData.c / 2; //divide by 2 because we use divisor=0 (which, actually divides the incoming clock tickrate by 2)

            const auto conv = sl::ConvertUnits(tscFrequency, sl::UnitBase::Decimal);
            Log("LAPIC timer frequency from cpuid leaf 0x15: %zu.%zu %sHz", LogLevel::Info, 
                conv.major, conv.minor, conv.prefix);
            return;
        }
        else if (maxBaseCpuidLeaf >= 0x16 && DoCpuid(0x16, 0, cpuidData).c != 0)
        {
            timerFrequency = cpuidData.c / 2;

            const auto conv = sl::ConvertUnits(tscFrequency, sl::UnitBase::Decimal);
            Log("LAPIC timer frequency from cpuid leaf 0x16: %zu.%zu %sHz", LogLevel::Info, 
                conv.major, conv.minor, conv.prefix);
            return;
        }
        else if (maxHyperCpuidLeaf && DoCpuid(0x4000'0010, 0, cpuidData).b != 0)
        {
            tscFrequency = cpuidData.b * 1000; //value is in KHz

            const auto conv = sl::ConvertUnits(tscFrequency, sl::UnitBase::Decimal);
            Log("LAPIC timer frequency from cpuid leaf 0x4000'0010: %zu.%zu %sHz", LogLevel::Info, 
                conv.major, conv.minor, conv.prefix);
            return;
        }

        WriteReg(LapicReg::LvtTimer, 1 << 16); //mask and stop timer
        WriteReg(LapicReg::TimerInitCount, 0);
        WriteReg(LapicReg::TimerDivisor, 0);

        const size_t controlOffset = [=]() -> size_t
        {
            size_t accum = 0;
            for (size_t i = 0; i < ControlRuns; i++)
            {
                WriteReg(LapicReg::LvtTimer, 1 << 16);
                WriteReg(LapicReg::TimerInitCount, 0xFFFF'FFFF);
                CalibrationSleep(0);
                const size_t end = ReadReg(LapicReg::TimerCount);

                accum += (0xFFFF'FFFF - end);
                if (dumpCalibData)
                    Log("TSC control run %zu: %zu", LogLevel::Verbose, i, 0xFFFF'FFFF - end);
            }
            accum /= ControlRuns;
            if (dumpCalibData)
                Log("TSC control offset: %zu ticks", LogLevel::Verbose, accum);
            return accum;
        }();

        //calibration time
        for (size_t i = 0; i < CalibrationRuns; i++)
        {
            WriteReg(LapicReg::LvtTimer, 1 << 16);
            WriteReg(LapicReg::TimerInitCount, 0xFFFF'FFFF);
            const TimerTickNanos realSleepTime = CalibrationSleep(sleepTime);

            const size_t rawData = (0xFFFF'FFFF - ReadReg(LapicReg::TimerCount)) - controlOffset;
            calibData[i] = (rawData * sleepTime) / realSleepTime;
            if (dumpCalibData)
            {
                Log("LAPIC calibration run %zu: rawTicks=%zu, sleepTime=%zu ns, fixedTicks=%zu",
                    LogLevel::Verbose, i, rawData, realSleepTime, calibData[i]);
            }
        }
        WriteReg(LapicReg::LvtTimer, 1 << 16);
        WriteReg(LapicReg::TimerInitCount, 0);

        ASSERT(CoalesceTimerRuns(calibData, CalibrationRuns - RequiredRuns, dumpCalibData), 
            "LAPIC timer calibration failed");
        timerFrequency = calibData[0] * SampleFrequency;
        auto conv = sl::ConvertUnits(timerFrequency, sl::UnitBase::Decimal);
        Log("LAPIC timer calibrated as: %zu.%zu %sHz", LogLevel::Info, conv.major, conv.minor, conv.prefix);
    }

    void LocalApic::CalibrateTsc(bool dumpCalibData, size_t maxBaseCpuidLeaf, size_t maxHyperCpuidLeaf)
    {
        //for TSC calibration, its similar to the lapic timer - first check if we can get it from cpuid,
        //then the hypervisor cpuid leaves, and then calibrate it if we have no other choices.

        tscFrequency = 0;
        size_t calibData[CalibrationRuns];
        const auto sleepTime = sl::TimeCount(SampleFrequency, 1).Rebase(sl::Nanos).ticks;

        CpuidLeaf cpuidData {};
        if (maxBaseCpuidLeaf >= 0x15 && DoCpuid(0x15, 0, cpuidData).b != 0 && cpuidData.a != 0)
        {
            tscFrequency = (cpuidData.c * cpuidData.b) / cpuidData.a;

            const auto conv = sl::ConvertUnits(tscFrequency, sl::UnitBase::Decimal);
            Log("TSC frequency from cpuid leaf 0x15: %zu.%zu %sHz", LogLevel::Info, 
                conv.major, conv.minor, conv.prefix);
            return;
        }
        else if (maxBaseCpuidLeaf >= 0x16 && DoCpuid(0x16, 0, cpuidData).a != 0)
        {
            tscFrequency = cpuidData.a * 1'000'000; //base frequency is in MHz

            const auto conv = sl::ConvertUnits(tscFrequency, sl::UnitBase::Decimal);
            Log("TSC frequency from cpuid leaf 0x16: %zu.%zu %sHz", LogLevel::Info, 
                conv.major, conv.minor, conv.prefix);
            return;
        }
        else if (maxHyperCpuidLeaf && DoCpuid(0x4000'0010, 0, cpuidData).a != 0)
        {
            tscFrequency = cpuidData.a * 1000; //value is in KHz

            const auto conv = sl::ConvertUnits(tscFrequency, sl::UnitBase::Decimal);
            Log("TSC frequency from cpuid leaf 0x4000'0010: %zu.%zu %sHz", LogLevel::Info, 
                conv.major, conv.minor, conv.prefix);
            return;
        }

        const size_t controlOffset = [=]() -> size_t
        {
            size_t accum = 0;
            for (size_t i = 0; i < ControlRuns; i++)
            {
                const size_t begin = ReadTsc();
                CalibrationSleep(0);
                const size_t end = ReadTsc();

                accum += end - begin;
                if (dumpCalibData)
                    Log("TSC control run %zu: %zu", LogLevel::Verbose, i, end - begin);
            }
            accum /= ControlRuns;
            if (dumpCalibData)
                Log("TSC control offset: %zu ticks", LogLevel::Verbose, accum);
            return accum;
        }();

        for (size_t i = 0; i < CalibrationRuns; i++)
        {
            const size_t begin = ReadTsc();
            const TimerTickNanos realSleepTime = CalibrationSleep(sleepTime);
            const size_t end = ReadTsc();

            const size_t rawData = (end - begin) - controlOffset;
            calibData[i] = (rawData * sleepTime) / realSleepTime; //oversleep correction
            if (dumpCalibData)
            {
                Log("TSC calibration run %zu: rawTicks=%zu, sleepTime=%zu ns, fixedTicks=%zu",
                    LogLevel::Verbose, i, rawData, realSleepTime, calibData[i]);
            }
        }

        ASSERT(CoalesceTimerRuns(calibData, CalibrationRuns - RequiredRuns, dumpCalibData), "TSC calibration failed");
        tscFrequency = calibData[0] * SampleFrequency;
        auto conv = sl::ConvertUnits(tscFrequency, sl::UnitBase::Decimal);
        Log("TSC calibrated as: %zu.%zu %sHz", LogLevel::Info, conv.major, conv.minor, conv.prefix);
    }

    static PvSystemTime* InitPvClock()
    {
        constexpr uint32_t HypervisorCpuidLeaf = 0x4000'0000;
        constexpr uint32_t PvClockPresent = 1 << 3;

        //we're running under a hypervisor, check if its KVM and if the pvclock MSR is supported
        CpuidLeaf leaf {};
        DoCpuid(HypervisorCpuidLeaf, 0, leaf);
        if (leaf.b != 0x4b4d564b || leaf.c != 0x564b4d56 || leaf.d != 0x4d)
            return nullptr; //validate KVMKVMKVM signature

        DoCpuid(HypervisorCpuidLeaf + 1, 0, leaf);
        if (!(leaf.a & PvClockPresent))
            return nullptr;

        auto maybePaddr = Core::PmAlloc();
        VALIDATE_(maybePaddr.HasValue(), nullptr);

        const uintptr_t paddr = *maybePaddr;
        WriteMsr(MsrPvSystemTime, paddr | 1); //bit 0 is the enable bit

        Log("KVM detected, enabled pvclock (io=0x%tx).", LogLevel::Info, paddr);
        return reinterpret_cast<PvSystemTime*>(paddr + hhdmBase);
    }

    bool LocalApic::Init()
    {
        VALIDATE_(CpuHasFeature(CpuFeature::Apic), false);
        VALIDATE_(CpuHasFeature(CpuFeature::Tsc), false);

        const uint64_t baseMsr = ReadMsr(MsrApicBase);
        VALIDATE(baseMsr & (1 << 11), false, "Local APIC globally disabled in MSR.");

        if (CpuHasFeature(CpuFeature::ApicX2))
        {
            x2Mode = true;
            WriteMsr(MsrApicBase, baseMsr | (1 << 10));
            Log("Local apic setup, in x2 mode.", LogLevel::Verbose);
        }
        else
        {
            x2Mode = false;
            mmioVmo = Services::CreateMmioVmo(baseMsr & ~0xFFFul, 0x1000, HatFlag::Mmio);
            VALIDATE_(mmioVmo != nullptr, false);

            auto maybeMmio = Services::VmAllocWired(mmioVmo, 0x1000, 0, VmViewFlag::Write);
            VALIDATE_(maybeMmio.HasValue(), false);
            mmio = *maybeMmio;

            Log("Local apic setup, mmio=%p (phys=0x%tx)", LogLevel::Verbose, mmio.ptr,
                baseMsr & ~0xFFFul);
        }

        auto madt = static_cast<const Services::Madt*>(Services::FindAcpiTable(Services::SigMadt));
        const bool picsPresent = madt == nullptr ? true : 
            (uint32_t)madt->flags & (uint32_t)Services::MadtFlags::PcAtCompat;
        if (IsBsp() && picsPresent)
        {
            Log("BSP is disabling legacy 8259 PICs.", LogLevel::Info);
            constexpr uint16_t PortCmd0 = 0x20;
            constexpr uint16_t PortCmd1 = 0xA0;
            constexpr uint16_t PortData0 = 0x21;
            constexpr uint16_t PortData1 = 0xA1;

            //start init sequence, with 4 command words.
            Out8(PortCmd0, 0x11);
            Out8(PortCmd1, 0x11);
            //set interrupt offsets
            Out8(PortData0, 0x20);
            Out8(PortData1, 0x28);
            //set ids and master/slave status
            Out8(PortData0, 4);
            Out8(PortData1, 2);
            //set mode (8086)
            Out8(PortData0, 1);
            Out8(PortData1, 1);
            //mask all interrupts
            Out8(PortData0, 0xFF);
            Out8(PortData1, 0xFF);
        }

        if (IsBsp() && CpuHasFeature(CpuFeature::VGuest))
            pvClock = InitPvClock();

        WriteReg(LapicReg::SpuriousConfig, IntrVectorSpurious | (1 << 8));

        //https://github.com/projectacrn/acrn-hypervisor/blob/master/hypervisor/arch/x86/lapic.c#L65
        for (size_t i = 8; i > 0; i--)
        {
            const LapicReg reg = static_cast<LapicReg>(static_cast<unsigned>(LapicReg::InService0) 
                + (i - 1) * 0x10);
            while (ReadReg(reg) != 0)
                SendEoi();
        }
        return true;
    }

    void LocalApic::CalibrateTimer()
    { 
        if (!CpuHasFeature(CpuFeature::AlwaysRunningApic) && IsBsp())
            Log("Always-running-apic not supported on this CPU.", LogLevel::Warning);
        if (!CpuHasFeature(CpuFeature::InvariantTsc) && IsBsp())
            Log("Invariant TSC not supported.", LogLevel::Warning);

        CpuidLeaf cpuidData;
        const size_t maxBaseCpuidLeaf = DoCpuid(0, 0, cpuidData).a;
        const size_t maxHyperCpuidLeaf = CpuHasFeature(CpuFeature::VGuest) 
            ? DoCpuid(0x4000'0000, 0, cpuidData).a : 0;

        const bool dumpCalibData = Core::GetConfigNumber("kernel.timer.dump_calibration_data", false);
        if (dumpCalibData)
        {
            Log("Calibration config: runs=%zu, allowedOutliers=%zu, sampleRate=%zuHz", 
                LogLevel::Info, CalibrationRuns, CalibrationRuns - RequiredRuns, SampleFrequency);
        }

        if (!CpuHasFeature(CpuFeature::TscDeadline))
            CalibrateLocalTimer(dumpCalibData, maxBaseCpuidLeaf, maxHyperCpuidLeaf);
        else
            useTscDeadline = true;
        CalibrateTsc(dumpCalibData, maxBaseCpuidLeaf, maxHyperCpuidLeaf);
    }

    TimerTickNanos LocalApic::ReadTscNanos() const
    {
        if (pvClock != nullptr)
        {
            PvSystemTime pvTime;

            while (true)
            {
                while (pvClock->version & 1)
                    sl::HintSpinloop();

                const uint32_t lastVersion = pvClock->version;
                sl::MemCopy(&pvTime, pvClock, sizeof(pvTime));

                if ((pvClock->version & 1) || pvClock->version != lastVersion)
                    continue;
                break;
            }

            TimerTickNanos time = ReadTsc() - pvTime.tscReference;
            if (pvTime.tscShift < 0)
                time >>= pvTime.tscShift;
            else
                time <<= pvTime.tscShift;
            time = (time * pvTime.tscToSystemMul) >> 32;

            return time + pvTime.systemTime;
        }

        return sl::TimeCount(tscFrequency, ReadTsc()).Rebase(sl::Nanos).ticks;
    }

    TimerTickNanos LocalApic::TimerMaxNanos() const
    { 
        if (useTscDeadline)
            return ~static_cast<uint64_t>(0);
        return sl::TimeCount(timerFrequency, ~static_cast<uint32_t>(0)).Rebase(sl::Nanos).ticks;
    }

    void LocalApic::ArmTimer(TimerTickNanos nanos, size_t vector)
    {
        if (useTscDeadline)
        {
            WriteReg(LapicReg::LvtTimer, vector | (1 << 18));
            WriteMsr(MsrTscDeadline, ReadMsr(MsrTsc) + nanos);
        }
        else
        {
            WriteReg(LapicReg::LvtTimer, vector);
            WriteReg(LapicReg::TimerInitCount, (timerFrequency * nanos) / sl::Nanos);
        }
    }

    void LocalApic::SendEoi()
    {
        WriteReg(LapicReg::Eoi, 0);
    }

    void LocalApic::SendIpi(size_t destAddr, bool urgent)
    {
        const uint32_t low = IntrVectorIpi | (urgent ? 0b100 << 8 : 0);

        //AMD says we dont have to do this, intel dont specify
        //so I'm taking the safe route and waiting until the
        //'delivery pending' bit is cleared before attempting
        //to send a new IPI.
        while (ReadReg(LapicReg::IcrLow) & (1 << 12))
            sl::HintSpinloop();

        //there's a special case for the x2 regs with ICR: it gets compressed
        //into a single MSR, instead of two separate regs.
        if (x2Mode)
        {
            WriteMsr((static_cast<uint32_t>(LapicReg::IcrLow) >> 4) + 0x800, 
                (destAddr << 32) | low);
        }
        else
        {
            //the IPI is sent when writing to IcrLow, so set the high register first.
            WriteReg(LapicReg::IcrHigh, destAddr << 24);
            WriteReg(LapicReg::IcrLow, low);
        }
    }

    bool SendIpi(size_t dest, bool urgent)
    {
        auto lapic = static_cast<LocalApic*>(GetLocalPtr(SubsysPtr::IntrCtrl));
        VALIDATE_(lapic != nullptr, false);
        lapic->SendIpi(dest, urgent);

        //IPIs are guarenteed to be delivered in both intel and amd specs
        return true;
    }


    enum class IoApicReg
    {
        Id = 0x0,
        Version = 0x1,
        TableBase = 0x10,
    };

    struct IoApic
    {
        sl::FwdListHook hook;
        sl::NativePtr mmio;
        uint32_t gsiBase;
        uint32_t pinCount;
        Services::VmObject* mmioVmo;
        sl::SpinLock lock;

        uint32_t ReadReg(IoApicReg reg)
        {
            sl::ScopedLock scopeLock(lock);
            mmio.Write<uint32_t>(static_cast<uint32_t>(reg));
            return mmio.Offset(0x10).Read<uint32_t>();
        }

        void WriteReg(IoApicReg reg, uint32_t value)
        {
            sl::ScopedLock scopeLock(lock);
            mmio.Write<uint32_t>(static_cast<uint32_t>(reg));
            mmio.Offset(0x10).Write<uint32_t>(value);
        }
    };

    struct SourceOverride
    {
        sl::FwdListHook hook;
        uint32_t incoming;
        uint32_t outgoing;
        sl::Opt<uint8_t> polarity;
        sl::Opt<uint8_t> mode;
        Services::VmObject* vmo;
    };

    sl::FwdList<IoApic, &IoApic::hook> ioapics;
    sl::FwdList<SourceOverride, &SourceOverride::hook> sourceOverrides;

    void InitIoApics()
    {
        using namespace Services;

        auto madt = static_cast<const Madt*>(FindAcpiTable(SigMadt));
        VALIDATE(madt != nullptr, , "MADT not found, aborting io apic discovery.");

        sl::CNativePtr scan = madt->sources;
        while (scan.raw < (uintptr_t)madt + madt->length)
        {
            auto src = scan.As<const MadtSource>();

            switch (src->type)
            {
            case MadtSourceType::IoApic:
            {
                auto apicSrc = scan.As<const MadtSources::IoApic>();
                IoApic* ioapic = NewWired<IoApic>();
                VALIDATE_(ioapic != nullptr, );

                ioapic->mmioVmo = CreateMmioVmo(apicSrc->mmioAddr, 0x1000, HatFlag::Mmio);
                VALIDATE_(ioapic->mmioVmo != nullptr, );
                auto maybeMmio = VmAllocWired(ioapic->mmioVmo, 0x1000, 0, VmViewFlag::Write);
                VALIDATE_(maybeMmio.HasValue(), );
                ioapic->mmio = *maybeMmio;

                ioapic->gsiBase = apicSrc->gsibase;
                ioapic->pinCount = (ioapic->ReadReg(IoApicReg::Version) >> 16) & 0xFF;
                ioapic->pinCount++;

                ioapics.PushBack(ioapic);
                Log("IOAPIC discovered: gsiBase=%u, pins=%u", LogLevel::Info, ioapic->gsiBase, 
                    ioapic->pinCount);
                break;
            }
            case MadtSourceType::SourceOverride:
            {
                auto soSrc = scan.As<const MadtSources::SourceOverride>();
                SourceOverride* so = NewWired<SourceOverride>();
                VALIDATE_(so != nullptr, );

                so->incoming = soSrc->source;
                so->outgoing = soSrc->mappedGsi;
                const auto polarity = soSrc->polarityModeFlags & MadtSources::PolarityMask;
                const auto triggerMode = soSrc->polarityModeFlags & MadtSources::TriggerModeMask;
                if (polarity != MadtSources::PolarityDefault)
                    so->polarity = polarity;
                if (triggerMode != MadtSources::TriggerModeDefault)
                    so->mode = triggerMode >> 2;

                sourceOverrides.PushBack(so);

                constexpr const char* PolarityStrs[] = { "default", "high", "", "low" };
                constexpr const char* ModeStrs[] = { "default", "edge", "", "level" };
                Log("Intr source override: %u -> %u, polarity=%s, mode=%s", LogLevel::Info,
                    so->incoming, so->outgoing, PolarityStrs[polarity], ModeStrs[triggerMode >> 2]);
                break;
            }

            default:
                break;
            }

            scan.raw += src->length;
        }
    }

    bool RoutePinInterrupt(size_t pin, size_t core, size_t vector)
    {
        for (auto it = ioapics.Begin(); it != ioapics.End(); ++it)
        {
            if (pin < it->gsiBase && pin >= it->gsiBase + it->pinCount)
                continue;

            Log("Routing pin interupt %zu to %zu:%zu", LogLevel::Verbose, pin, core, vector);
            SourceOverride* override = nullptr;
            for (auto iso = sourceOverrides.Begin(); iso != sourceOverrides.End(); ++iso)
            {
                if (iso->incoming != pin)
                    continue;

                Log("Pin interrupt has acpi override: %" PRIu32" -> %" PRIu32, LogLevel::Verbose, iso->incoming, iso->outgoing);
                override = &*iso;
                break;
            }

            using namespace Services::MadtSources;
            const uint64_t polarity = override != nullptr && *override->polarity == PolarityLow ? 1 : 0;
            const uint64_t mode = override != nullptr && *override->mode == TriggerModeLevel ? 1 : 0;
            const uint64_t redirect = (vector & 0xFF) | (polarity << 13) | (mode << 15) | (core << 56);
            pin -= it->gsiBase;

            it->WriteReg((IoApicReg)((unsigned)IoApicReg::TableBase + pin * 2), redirect);
            it->WriteReg((IoApicReg)((unsigned)IoApicReg::TableBase + pin * 2 + 1), redirect >> 32);

            return true;
        }

        return false;
    }
}
