#include <arch/x86_64/Apic.h>
#include <arch/x86_64/Cpuid.h>
#include <arch/x86_64/Timers.h>
#include <core/Log.h>
#include <core/WiredHeap.h>
#include <core/Config.h>
#include <services/AcpiTables.h>
#include <services/Vmm.h>
#include <Locks.h>
#include <Maths.h>
#include <containers/List.h>
#include <UnitConverter.h>

namespace Npk
{
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

        WriteReg(LapicReg::SpuriousConfig, IntrVectorSpurious | (1 << 8));
        return true;
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

    void LocalApic::CalibrateTimer()
    { 
        constexpr size_t TotalRuns = 8;
        constexpr size_t RequiredRuns = 5;
        constexpr size_t SampleFrequency = 100;

        if (!CpuHasFeature(CpuFeature::AlwaysRunningApic) && IsBsp())
            Log("Always-running-apic not supported on this CPU.", LogLevel::Warning);
        if (!CpuHasFeature(CpuFeature::InvariantTsc) && IsBsp())
            Log("Invariant TSC not supported.", LogLevel::Warning);

        const bool dumpCalibData = Core::GetConfigNumber("kernel.timer.dump_calibration_data", false);
        if (dumpCalibData)
        {
            Log("Calibration config: runs=%zu, allowedOutliers=%zu, sampleRate=%zuHz", 
                LogLevel::Info, TotalRuns, TotalRuns - RequiredRuns, SampleFrequency);
        }
        useTscDeadline = CpuHasFeature(CpuFeature::TscDeadline);

        size_t calibData[TotalRuns];
        const auto sleepTime = sl::TimeCount(SampleFrequency, 1).Rebase(sl::Nanos).ticks;
        if (!useTscDeadline)
        {
            //we can't use the tsc as the interrupt timer source, so we'll
            //need to calibrate the lapic timer frequency.
            WriteReg(LapicReg::LvtTimer, 1 << 16); //mask and stop timer
            WriteReg(LapicReg::TimerInitCount, 0);
            WriteReg(LapicReg::TimerDivisor, 0);

            for (size_t i = 0; i < TotalRuns; i++)
            {
                WriteReg(LapicReg::LvtTimer, 1 << 16);
                WriteReg(LapicReg::TimerInitCount, 0xFFFF'FFFF);
                const TimerTickNanos realSleepTime = CalibrationSleep(sleepTime);

                const size_t rawData = (uint32_t)-1 - ReadReg(LapicReg::TimerCount);
                calibData[i] = (rawData * sleepTime) / realSleepTime;
                if (dumpCalibData)
                {
                    Log("LAPIC calibration run %zu: rawTicks=%zu, sleepTime=%zu ns, fixedTicks=%zu",
                        LogLevel::Verbose, i, rawData, realSleepTime, calibData[i]);
                }
            }
            WriteReg(LapicReg::LvtTimer, 1 << 16);
            WriteReg(LapicReg::TimerInitCount, 0);

            ASSERT(CoalesceTimerRuns(calibData, TotalRuns - RequiredRuns, dumpCalibData), 
                "LAPIC timer calibration failed");
            timerFrequency = calibData[0] * SampleFrequency;
            auto conv = sl::ConvertUnits(timerFrequency);
            Log("LAPIC timer calibrated as: %zu.%zu %sHz", LogLevel::Info, conv.major, conv.minor, conv.prefix);
        }

        //TODO: if cpuid leaf is present for tsc details, use that and dont bother calibrating
        for (size_t i = 0; i < TotalRuns; i++)
        {
            const size_t begin = ReadTsc();
            const TimerTickNanos realSleepTime = CalibrationSleep(sleepTime);
            const size_t end = ReadTsc();

            const size_t rawData = end - begin;
            calibData[i] = (rawData * sleepTime) / realSleepTime; //oversleep correction
            if (dumpCalibData)
            {
                Log("TSC calibration run %zu: rawTicks=%zu, sleepTime=%zu ns, fixedTicks=%zu",
                    LogLevel::Verbose, i, rawData, realSleepTime, calibData[i]);
            }
        }

        //TODO: account for time taken during measuring code. Sleep for 0time and compare results,
        //subtract that from all future measurements?
        ASSERT(CoalesceTimerRuns(calibData, TotalRuns - RequiredRuns, dumpCalibData), "TSC calibration failed");
        tscFrequency = calibData[0] * SampleFrequency;
        auto conv = sl::ConvertUnits(tscFrequency);
        Log("TSC calibrated as: %zu.%zu %sHz", LogLevel::Info, conv.major, conv.minor, conv.prefix);
    }

    TimerTickNanos LocalApic::ReadTscNanos()
    {
        return sl::TimeCount(tscFrequency, ReadTsc()).Rebase(sl::Nanos).ticks;
    }

    TimerTickNanos LocalApic::TimerMaxNanos()
    { 
        if (useTscDeadline)
            //return sl::ScaledTime::FromFrequency(tscFrequency).ToNanos() * static_cast<uint64_t>(~0);
            return static_cast<uint64_t>(~0);
        return sl::TimeCount(timerFrequency, static_cast<uint32_t>(~0)).Rebase(sl::Nanos).ticks;
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
            WriteReg(LapicReg::TimerInitCount, nanos * sl::TimeCount(timerFrequency, 1).Rebase(sl::Nanos).ticks);
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
