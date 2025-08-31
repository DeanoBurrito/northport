#include <hardware/Plat.hpp>
#include <hardware/Entry.hpp>
#include <hardware/x86_64/Mmu.hpp>
#include <hardware/x86_64/LocalApic.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <hardware/x86_64/PvClock.hpp>
#include <hardware/x86_64/Tsc.hpp>
#include <AcpiTypes.hpp>
#include <Core.hpp>
#include <Mmio.hpp>
#include <Maths.hpp>
#include <UnitConverter.hpp>

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

    bool hasPvClocks;
    sl::Span<uint64_t> savedMtrrs;

    void PlatInitFull(uintptr_t& virtBase)
    { 
        InitReferenceTimers(virtBase);
        NPK_ASSERT(CalibrateTsc()); //TODO: fallback for systems without tsc?

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
            NPK_ASSERT(mtrrCount * sizeof(uint64_t) < PageSize());

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
        //setting cpu locals must be the first thing we do, since logging
        //relies on this for cpu-id
        auto* locals = reinterpret_cast<const CoreLocalHeader*>(localStorage);
        SetMyLocals(localStorage, locals->swId);
        Log("Core %zu is online.", LogLevel::Info, MyCoreId());

        RestoreMtrrs(savedMtrrs);
        CommonCpuSetup();

        ThreadContext idleContext {};
        BringCpuOnline(&idleContext);
        NPK_ASSERT(CalibrateTsc());
        InitApLapic();

        Log("AP init thread done, becoming idle thread.", LogLevel::Verbose);
        IntrsOn();
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

    //TODO: investigate APs starting other cpus, if we run on systems with a large number of cpus,
    //this would help reduce startup time.
    void PlatBootAps(uintptr_t stacks, uintptr_t perCpuStores, size_t perCpuStride)
    {
        NPK_ASSERT(apBootPage != Paddr());
        NPK_ASSERT(kernelMap >> 32 == 0); //we can only load 32-bits into cr3 from the spinup code
        NPK_ASSERT(!savedMtrrs.Empty());

        auto maybeMadt = GetAcpiTable(sl::SigMadt);
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

        auto madt = static_cast<const sl::Madt*>(*maybeMadt);
        for (auto source = sl::NextMadtSubtable(madt); source != nullptr; source = sl::NextMadtSubtable(madt, source))
        {
            uint32_t targetLapicId = MyLapicId();
            if (source->type == sl::MadtSourceType::LocalApic)
            {
                auto src = static_cast<const sl::MadtSources::LocalApic*>(source);
                if (src->flags.Has(sl::MadtSources::LocalApicFlag::Enabled) || 
                    src->flags.Has(sl::MadtSources::LocalApicFlag::OnlineCapable))
                    targetLapicId = src->apicId;
            }
            else if (source->type == sl::MadtSourceType::LocalX2Apic)
            {
                auto src = static_cast<const sl::MadtSources::LocalX2Apic*>(source);
                if (src->flags.Has(sl::MadtSources::LocalApicFlag::Enabled) || 
                    src->flags.Has(sl::MadtSources::LocalApicFlag::OnlineCapable))
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
        auto ticks = sl::TimeCount(expiry.Frequency, expiry.epoch).Rebase(MyTscFrequency()).ticks;
        ArmTscInterrupt(ticks);
    }

    sl::TimePoint PlatReadTimestamp()
    {
        if (hasPvClocks)
            return { ReadPvSystemTime() };

        const auto timestamp = sl::TimeCount(MyTscFrequency(), ReadTsc());
        const auto ticks = timestamp.Rebase(sl::TimePoint::Frequency).ticks;
        return { ticks };
    }
    //ReadPvSystemTime() returns nanoseconds
    static_assert(sl::TimePoint::Frequency == sl::TimeScale::Nanos);

    void PlatSendIpi(void* id)
    {
        const uint32_t apicId = reinterpret_cast<uintptr_t>(id);
        SendIpi(apicId, IpiType::Fixed, LapicIpiVector);
    }
}
