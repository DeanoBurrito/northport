#include <hardware/Plat.hpp>
#include <hardware/x86_64/Mmu.hpp>
#include <hardware/x86_64/LocalApic.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <hardware/x86_64/PvClock.hpp>
#include <AcpiTypes.hpp>
#include <KernelApi.hpp>
#include <Maths.h>

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

    static void InitReferenceTimer()
    {
    }

    static uint64_t ReadReferenceTimer()
    {
        NPK_UNREACHABLE();
    }

    CPU_LOCAL(uint64_t, tscFreq);
    bool hasPvClocks;

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
         */

        CpuidLeaf cpuid {};
        const size_t baseLeaves = DoCpuid(BaseLeaf, 0, cpuid).a;

        //1.
        DoCpuid(0x15, 0, cpuid);
        if (baseLeaves >= 0x15 && cpuid.b != 0 && cpuid.a != 0)
        {
            Log("TSC frequency acquired from cpuid 0x15: %u / %u * %u", LogLevel::Trace,
                cpuid.c, cpuid.b, cpuid.a);
            return (cpuid.c * cpuid.b) / cpuid.a;
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
        const size_t neededRuns = sl::Clamp<size_t>(ReadConfigUint("npk.x86.tsc_needed_runs", 5), 1, calibRuns);
        const size_t controlRuns = sl::Clamp<size_t>(ReadConfigUint("npkl.x86.tsc_control_runs", 5), 1, calibRuns);

        size_t controlOffset = 0;
        size_t calibData[MaxCalibRuns];

        //control runs, determine time taken to read reference timer
        for (size_t i = 0; i < controlRuns; i++)
        {
            const size_t tscBegin = ReadTsc();
            ReadReferenceTimer();
            const size_t tscEnd = ReadTsc();

            controlOffset += tscEnd - tscBegin;
        }

        controlOffset /= controlRuns;
        Log("Control offset for reference timer: %zu tsc ticks", LogLevel::Trace, controlOffset);

        for (size_t i = 0; i < calibRuns; i++)
        {
            const size_t tscBegin = ReadTsc();
            /*(
            while (ReadReferenceTimer() < refEndTime)
                sl::HintSpinloop();
            */
            const size_t tscEnd = ReadTsc();

            calibData[i] = (tscEnd - tscBegin) - controlOffset;
            Log("TSC calibratun run: begin=%zu, end=%zu, total (minus control)=%zu", LogLevel::Verbose,
                tscBegin, tscEnd, calibData[i]);

            NPK_UNREACHABLE(); //TODO: finish calibrating by coalescing runs
        }
    }

    sl::Span<uint64_t> savedMtrrs;

    void PlatInitFull(uintptr_t& virtBase)
    { 
        InitReferenceTimer();
        *tscFreq = CalibrateTsc();

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

    static void ApEntryFunc(uint64_t localStorage)
    {
        RestoreMtrrs(savedMtrrs);

        auto* locals = reinterpret_cast<const CoreLocalHeader*>(localStorage);
        SetMyLocals(localStorage, locals->swId);

        Log("Core %zu is alive!", LogLevel::Debug, MyCoreId());
        while (true)
            WaitForIntr();
        //TODO: init cr0 + cr4 + efer
        NPK_UNREACHABLE();
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
        auto scan = reinterpret_cast<const char*>(*maybeMadt) + sizeof(Madt);
        const char* end = scan + static_cast<Madt*>(*maybeMadt)->length - sizeof(Madt);

        while (scan < end)
        {
            const auto source = reinterpret_cast<const MadtSource*>(scan);
            scan += source->length;

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
}
