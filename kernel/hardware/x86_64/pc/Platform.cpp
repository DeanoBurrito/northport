#include <hardware/Plat.hpp>
#include <hardware/Entry.hpp>
#include <hardware/x86_64/Mmu.hpp>
#include <hardware/x86_64/LocalApic.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <hardware/x86_64/PvClock.hpp>
#include <AcpiTypes.hpp>
#include <KernelApi.hpp>
#include <Scheduler.hpp>
#include <Mmio.h>
#include <Maths.h>
#include <UnitConverter.h>

extern "C"
{
    extern char SpinupBlob[];
    extern char _EndOfSpinupBlob[];
}

namespace Npk
{
    struct BootInfo
    {
        uint64_t localStorage;
        uint64_t entry;
        uint64_t stack;
        sl::Atomic<uint64_t> booted;
        uint64_t cr3;
    };

    void PlatInitEarly()
    {} //no-op

    size_t PlatGetCpuCount(InitState& state)
    {
        auto maybeRsdp = GetConfigRoot(ConfigRootType::Rsdp);
        //TODO: modify bootloader page tables to map acpi tables as we need
        //TODO: find and parse madt early on, so we can get a total cpu count
        return 4;
    }

    void PlatInitDomain0(InitState& state)
    { (void)state; } //no-op

    enum class HpetReg
    {
        Capabilities = 0,
        Config = 0x10,
        IntrStatus = 0x20,
        MainCounter = 0xF0,
        Timer0Config = 0x100,
        Timer0Value = 0x108,
        Timer0IntrRouting = 0x110,
    };

    static uint64_t acpiTimerAddr;
    static bool acpiTimerIsMmio;
    static bool acpiTimerIs32bit;

    static bool TryInitPmTimer(Fadt* table, uintptr_t& virtBase)
    {
        if (ReadConfigUint("npk.x86.ignore_acpi_pm_timer", false))
            return false;
        if (table == nullptr)
            return false;
        if (table->flags.Has(FadtFlag::HwReducedAcpi))
            return false;

        acpiTimerAddr = 0;
        if (table->length >= offsetof(Fadt, xPmTimerBlock) && table->xPmTimerBlock.address != 0)
        {
            acpiTimerAddr = table->xPmTimerBlock.address;
            const auto type = table->xPmTimerBlock.type;
            NPK_CHECK(type == AcpiAddrSpace::IO || type == AcpiAddrSpace::Memory, false);
            acpiTimerIsMmio = type == AcpiAddrSpace::Memory;
        }
        else if (table->length >= offsetof(Fadt, pmTimerBlock) && table->pmTimerBlock != 0)
        {
            acpiTimerAddr = table->pmTimerBlock;
            acpiTimerIsMmio = false;
        }
        if (acpiTimerAddr == 0)
            return false;

        acpiTimerIs32bit = table->flags.Has(FadtFlag::TimerValExt);
        Log("ACPI timer available as reference timer: addr=0x%tx (%s), width=%s",
            LogLevel::Info, acpiTimerAddr, acpiTimerIsMmio ? "mmio" : "pio", 
            acpiTimerIs32bit ? "32-bit" : "24-bit");

        return true;
    }

    static sl::MmioRegisters<HpetReg, uint64_t> hpetRegs;
    static uint64_t hpetFreq;

    static bool TryInitHpet(Hpet* table, uintptr_t& virtBase)
    {
        if (ReadConfigUint("npk.x86.ignore_hpet", false))
            return false;
        if (table == nullptr)
            return false;

        //the HPET spec *implies* the timer blocks must live in memory space, but
        //it does seem to allow for IO space blocks. For now we only support memory space.
        NPK_CHECK(table->baseAddress.type == AcpiAddrSpace::Memory, false);

        auto ret = ArchAddMap(MyKernelMap(), virtBase, table->baseAddress.address, 
            MmuFlag::Write | MmuFlag::Mmio);
        if (ret != MmuError::Success)
            return false;

        hpetRegs = virtBase;
        virtBase += PageSize();

        const uint64_t caps = hpetRegs.Read(HpetReg::Capabilities);

        const bool is64bit = caps & (1 << 13);
        const uint8_t timers = 1 + ((caps >> 8) & 0xF);
        const uint64_t period = (caps >> 32) & 0xFFFF'FFFF;

        hpetFreq = sl::Femtos / period;
        const auto freq = sl::ConvertUnits(hpetFreq);
        Log("HPET available as reference timer: counter=%s, timers=%u, freq=%zu.%zu %sHz",
            LogLevel::Info, is64bit ? "64-bit" : "32-bit", timers, freq.major, 
            freq.minor, freq.prefix);

        return true;
    }

    static void InitReferenceTimer(uintptr_t& virtBase)
    {
        if (auto fadt = GetAcpiTable(SigFadt); 
            fadt.HasValue() && TryInitPmTimer(static_cast<Fadt*>(*fadt), virtBase))
            return;

        auto maybeHpet = GetAcpiTable(SigHpet);
        if (auto hpet = GetAcpiTable(SigHpet);
            hpet.HasValue() && TryInitHpet(static_cast<Hpet*>(*hpet), virtBase))
            return;

        //Afaik there isn't a way to test for the presence of a PIT, you just have to know.
        //I have included a command line flag in case we ever run on a system where
        //it's not present.
        const bool forceIgnorePit = ReadConfigUint("npk.x86.ignore_pit", false);
        //dont laugh at this lol: but the PIT is the last option for a reference timer, so it's
        //not available we cant continue.
        NPK_ASSERT(!forceIgnorePit);

        Log("PIT selected as calibration reference timer", LogLevel::Verbose);
    }

    static uint64_t ReferenceSleep(uint64_t sleepNanos)
    {
        NPK_UNREACHABLE();
        if (acpiTimerAddr != 0)
        {
        }
        else if (hpetRegs.BaseAddress() != 0)
        {
        }
        else
        {
            //PIT
        }
    }

    CPU_LOCAL(uint64_t, tscFreq);
    bool hasPvClocks;

    static sl::Opt<uint64_t> CoalesceTimerData(sl::Span<uint64_t> runs, size_t allowedOutliers)
    {
        uint64_t mean = 0;
        for (size_t i = 0; i < runs.Size(); i++)
            mean += runs[i];
        mean /= runs.Size();

        const uint64_t deviation = sl::StandardDeviation(runs);

        size_t validCount = 0;
        uint64_t accumulator = 0;
        for (size_t i = 0; i < runs.Size(); i++)
        {
            if (runs[i] < mean - deviation || runs[i] > mean + deviation)
                continue;

            validCount++;
            accumulator += runs[i];
        }

        if (validCount < runs.Size() - allowedOutliers)
            return {};

        accumulator /= validCount;
        return accumulator;
    }

    static uint64_t CalibrateTsc()
    {
        /* Calibrating the tsc, or: "Why let things that should be simple, be simple".
         * We try a number of ways to calibrate the tsc, moving on to the next if we fail:
         * 1. read the values from cpuid leaf 0x15
         * 2. read the values from cpuid leaf 0x16
         * 3. read the values from cpuid leaf 0x4000'0010
         * 4. calibrate against another timer:
         *  4a. acpi pm timer
         *  4b. hpet
         *  4c. pit
         * There is also the option of the user explicitly telling us the tsc freq, if they want.
         */

        if (auto freq = ReadConfigUint("npk.x86.tsc_freq_override", 0); freq != 0)
        {
            Log("TSC frequency set to %zuHz by command line override.", LogLevel::Trace,
                freq);
            return freq;
        }

        CpuidLeaf cpuid {};
        const size_t baseLeaves = DoCpuid(BaseLeaf, 0, cpuid).a;

        //1.
        DoCpuid(0x15, 0, cpuid);
        if (baseLeaves >= 0x15 && cpuid.b != 0 && cpuid.a != 0)
        {
            const uint64_t freq = (cpuid.c * cpuid.b) / cpuid.a;
            Log("TSC frequency acquired from cpuid 0x15: %u / %u * %u = %luHz", LogLevel::Trace,
                cpuid.c, cpuid.b, cpuid.a, freq);
            return freq;
        }

        //2.
        DoCpuid(0x16, 0, cpuid);
        if (baseLeaves >= 0x16 && cpuid.a != 0)
        {
            Log("TSC frequency acquired from cpuid 0x15: %uMHz", LogLevel::Trace, cpuid.a);
            return cpuid.a * 1'000'000;
        }

        //3.
        DoCpuid(HypervisorLeaf, 0, cpuid);
        if (cpuid.a >= 0x10 && DoCpuid(HypervisorLeaf + 0x10, 0, cpuid).a != 0)
        {
            Log("TSC frequency acquired from cpuid 0x4000'0010: %uKHz", LogLevel::Trace, cpuid.a);
            return cpuid.a * 1000;
        }

        //4.
        constexpr size_t MaxCalibRuns = 64;
        const size_t calibRuns = sl::Clamp<size_t>(ReadConfigUint("npk.x86.tsc_calibration_runs", 10), 1, MaxCalibRuns);
        const size_t sampleFreq = sl::Clamp<size_t>(ReadConfigUint("npk.x86.tsc_sample_freq", 100), 10, 1000);
        const size_t neededRuns = sl::Clamp<size_t>(ReadConfigUint("npk.x86.tsc_needed_runs", 7), 1, calibRuns);
        const size_t controlRuns = sl::Clamp<size_t>(ReadConfigUint("npk.x86.tsc_control_runs", 5), 1, calibRuns);
        const bool dumpCalibData = ReadConfigUint("npk.x86.tsc_dump_calibration", true);

        size_t controlOffset = 0;
        uint64_t calibData[MaxCalibRuns];
        uint64_t calibNanos = sl::TimeCount(sampleFreq, 1).Rebase(sl::Nanos).ticks;
        Log("Calibrating TSC: sampling=%zu hz, runs=%zu (mulligans=%zu, control=%zu)", LogLevel::Trace,
            sampleFreq, calibRuns, calibRuns - neededRuns, controlRuns);

        //control runs, determine time taken to read reference timer
        for (size_t i = 0; i < controlRuns; i++)
        {
            const size_t tscBegin = ReadTsc();
            ReferenceSleep(0);
            const size_t tscEnd = ReadTsc();

            controlOffset += tscEnd - tscBegin;
        }

        controlOffset /= controlRuns;
        Log("Control offset for reference timer: %zu tsc ticks", LogLevel::Trace, controlOffset);

        for (size_t i = 0; i < calibRuns; i++)
        {
            const uint64_t tscBegin = ReadTsc();
            const uint64_t realCalibNanos = ReferenceSleep(calibNanos);
            const uint64_t tscEnd = ReadTsc();

            if (realCalibNanos == 0)
            {
                //something went wrong with the calibration sleep (massive SMI?)
                calibData[i] = 0;
                continue;
            }

            calibData[i] = (tscEnd - tscBegin) - controlOffset;
            calibData[i] = (calibData[i] * calibNanos) / realCalibNanos; //oversleep correction
            if (dumpCalibData)
            {
                Log("TSC calibratun run: begin=%zu, end=%zu, adjusted=%zu", LogLevel::Verbose,
                    tscBegin, tscEnd, calibData[i]);
            }
        }

        const auto maybeTscPeriod = CoalesceTimerData({ calibData, calibRuns }, calibRuns - neededRuns);
        NPK_ASSERT(maybeTscPeriod.HasValue());

        const uint64_t tscFreq = *maybeTscPeriod * sampleFreq;
        const auto conv = sl::ConvertUnits(tscFreq);
        Log("TSC calibrated as %zu Hz (%zu.%zu %sHz)", LogLevel::Info, tscFreq,
            conv.major, conv.minor, conv.prefix);
        return tscFreq;
    }

    sl::Span<uint64_t> savedMtrrs;

    void PlatInitFull(uintptr_t& virtBase)
    { 
        InitReferenceTimer(virtBase);
        *tscFreq = CalibrateTsc(); //TODO: fallback for systems without tsc?

        if (CpuHasFeature(CpuFeature::VGuest))
            hasPvClocks = TryInitPvClocks(virtBase);

        if (CpuHasFeature(CpuFeature::Mtrr))
        {
            const uint64_t mtrrCap = ReadMsr(Msr::MtrrCap);
            const bool fixed = mtrrCap & 0x100;
            const size_t vcount = mtrrCap & 0xFF;
            
            Log("Saving BSP MTRR values: fixed=%s, vcnt=%zu", LogLevel::Info,
                fixed ? "yes (11 MTRRs)" : "no", vcount);

            const size_t mtrrCount = vcount * 2 + (fixed ? 11 : 0);
            auto storage = AllocPage(true);
            NPK_ASSERT(storage != nullptr);

            ArchAddMap(MyKernelMap(), virtBase, LookupPagePaddr(storage), MmuFlag::Write);
            savedMtrrs = sl::Span<uint64_t>(reinterpret_cast<uint64_t*>(virtBase), mtrrCount);
            virtBase += PageSize();

            SaveMtrrs(savedMtrrs);
        }
    }

    static void RemoteFunction(void* arg)
    {
        (void)arg;
        Log("Hello, I am a remotely-called function from core %p!", LogLevel::Debug, arg);
    }

    static void ApEntryFunc(uint64_t localStorage)
    {
        //setting cpu locals must be the first thing we do, since logging
        //relies on this for cpu-id
        auto* locals = reinterpret_cast<const CoreLocalHeader*>(localStorage);
        SetMyLocals(localStorage, locals->swId);
        Log("Core %zu is online.", LogLevel::Info, MyCoreId());

        RestoreMtrrs(savedMtrrs);
        CommonCpuSetup();
        InitApLapic();

        ThreadContext idleContext {};
        BringCpuOnline(&idleContext);

        Log("AP init thread done, becoming idle thread.", LogLevel::Verbose);
        IntrsOn();

        SmpMail mail {};
        mail.data.onComplete = nullptr;
        mail.data.arg = reinterpret_cast<void*>(MyCoreId());
        mail.data.function = RemoteFunction;
        SendMail(0, &mail);

        while (true)
            WaitForIntr();
    }

    static bool TryStartAp(uint32_t lapicId, BootInfo* bootInfo, sl::TimeCount deAssertDelay, sl::TimeCount sipiDelay)
    {
        Log("Sending INIT IPI to lapic-%u", LogLevel::Trace, lapicId);
        SendIpi(lapicId, IpiType::Init, 0);

        PlatStallFor(deAssertDelay);
        NPK_CHECK(LastIpiSent(), false);

        Log("Sending INIT de-assert IPI to lapic-%u", LogLevel::Trace, lapicId);
        SendIpi(lapicId, IpiType::InitDeAssert, 0);

        NPK_CHECK(LastIpiSent(), false);

        for (size_t i = 0; i < 2; i++)
        {
            Log("Sending startup IPI %zu to lapic-%u", LogLevel::Trace, i, lapicId);
            SendIpi(lapicId, IpiType::Startup, apBootPage >> PfnShift());

            PlatStallFor(sipiDelay);
            NPK_CHECK(LastIpiSent(), false);

            if (bootInfo->booted.Load() == 1)
            {
                Log("Successfully started AP at lapic-%u", LogLevel::Verbose, lapicId);
                return true;
            }
        }

        Log("Failed to start AP at lapic-%u, no response.", LogLevel::Error, lapicId);
        return false;
    }

    void PlatBootAps(uintptr_t stacks, uintptr_t perCpuStores, size_t perCpuStride)
    {
        NPK_ASSERT(apBootPage != Paddr());
        NPK_ASSERT(kernelMap >> 32 == 0); //we can only load 32-bits into cr3 from the spinup code
        NPK_ASSERT(!savedMtrrs.Empty());

        auto maybeMadt = GetAcpiTable(SigMadt);
        NPK_CHECK(maybeMadt.HasValue(), );

        const bool modernDelays = ReadConfigUint("npk.x86.lapic_modern_delays", false); //TODO: detect the default programatically
        const auto initDeAssertDelay = modernDelays ? 0_ms : 10_ms;
        const auto sipiDelay = modernDelays ? 10_us : 300_us;
        NPK_ASSERT((MyLapicVersion() & 0xF0) == 0x10); //0x1X versions are the integrated lapics, which is all we support.

        PageAccessRef bootPageRef = AccessPage(apBootPage);
        const size_t infoOffset = ((uintptr_t)_EndOfSpinupBlob - (uintptr_t)SpinupBlob)
            - sizeof(BootInfo);

        BootInfo* bootInfo = reinterpret_cast<BootInfo*>(infoOffset +
            reinterpret_cast<uintptr_t>(bootPageRef->value));
        const size_t stackStride = KernelStackSize() + PageSize();

        bootInfo->entry = reinterpret_cast<uint64_t>(&ApEntryFunc);
        bootInfo->cr3 = reinterpret_cast<uint64_t>(kernelMap);

        size_t idAlloc = 1; //id=0 is BSP (the currently executing core)

        auto madt = static_cast<const Madt*>(*maybeMadt);
        for (auto source = NextMadtSubtable(madt); source != nullptr; source = NextMadtSubtable(madt, source))
        {
            uint32_t targetLapicId = MyLapicId();
            if (source->type == MadtSourceType::LocalApic)
            {
                auto src = static_cast<const MadtSources::LocalApic*>(source);
                if (src->flags.Has(MadtSources::LocalApicFlag::Enabled) || 
                    src->flags.Has(MadtSources::LocalApicFlag::OnlineCapable))
                    targetLapicId = src->apicId;
            }
            else if (source->type == MadtSourceType::LocalX2Apic)
            {
                auto src = static_cast<const MadtSources::LocalX2Apic*>(source);
                if (src->flags.Has(MadtSources::LocalApicFlag::Enabled) || 
                    src->flags.Has(MadtSources::LocalApicFlag::OnlineCapable))
                    targetLapicId = src->apicId;
            }
            else
                continue;

            if (targetLapicId == MyLapicId())
                continue;

            Log("Preparing to start AP at lapic-%u", LogLevel::Verbose, targetLapicId);
            bootInfo->stack = stacks + idAlloc * stackStride;
            bootInfo->stack -= 8;
            bootInfo->localStorage = perCpuStores + idAlloc * perCpuStride;
            bootInfo->booted = 0;

            auto locals = reinterpret_cast<CoreLocalHeader*>(bootInfo->localStorage);
            locals->swId = idAlloc;
            locals->selfAddr = bootInfo->localStorage;

            //compiler memory barrier: ensure all above memory ops have dont move beyond this point
            asm("" ::: "memory");

            if (!TryStartAp(targetLapicId, bootInfo, initDeAssertDelay, sipiDelay))
                continue;

            idAlloc++;
        }

        Log("AP startup done, %zu cpus running", LogLevel::Info, idAlloc);
    }

    void PlatSetAlarm(sl::TimePoint expiry)
    {
        ArmTscInterrupt(sl::TimeCount(expiry.Frequency, expiry.epoch).Rebase(*tscFreq).ticks);
    }

    sl::TimePoint PlatReadTimestamp()
    {
        if (hasPvClocks)
            return { ReadPvSystemTime() };
        return { sl::TimeCount(*tscFreq, ReadTsc()).Rebase(sl::TimePoint::Frequency).ticks };
    }
    static_assert(sl::TimePoint::Frequency == sl::TimeScale::Nanos); //ReadPvSystemTime() returns nanos

    void PlatSendIpi(void* id)
    {
        const uint32_t apicId = reinterpret_cast<uintptr_t>(id);
        SendIpi(apicId, IpiType::Fixed, LapicIpiVector);
    }
}
