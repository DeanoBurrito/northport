#include <private/Entry.hpp>
#include <private/Core.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <hardware/x86_64/LocalApic.hpp>
#include <hardware/x86_64/Private.hpp>
#include <hardware/x86_64/Msr.hpp>
#include <hardware/x86_64/Tsc.hpp>
#include <lib/AcpiTypes.hpp>
#include <lib/Maths.hpp>
#include <Core.hpp>
#include <Vm.hpp>

namespace Npk
{
    static bool IsValidLapicEntry(uint32_t* outId, const sl::MadtSource& source)
    {
        using namespace sl::MadtSources;

        if (source.type == sl::MadtSourceType::LocalApic)
        {
            auto& entry = static_cast<const LocalApic&>(source);

            if (!entry.flags.Has(LocalApicFlag::Enabled))
                return false;
            
            if (outId != nullptr)
                *outId = entry.apicId;

            return true;
        }
        else if (source.type == sl::MadtSourceType::LocalX2Apic)
        {
            auto& entry = static_cast<const LocalX2Apic&>(source);

            if (!entry.flags.Has(LocalApicFlag::Enabled))
                return false;

            if (outId != nullptr)
                *outId = entry.apicId;

            return true;
        }

        return false;
    }

    size_t HwGetCpuCount()
    {
        auto maybeMadt = GetAcpiTable(sl::SigMadt);
        if (!maybeMadt.HasValue())
            return 1;

        auto madt = static_cast<const sl::Madt*>(*maybeMadt);
        size_t accum = 0;

        for (auto source = sl::NextMadtSubtable(madt); source != nullptr;
            source = sl::NextMadtSubtable(madt, source))
        {
            if (IsValidLapicEntry(nullptr, *source))
                accum++;
        }

        NPK_ASSERT(accum > 0);
        return accum;
    }

    struct BootInfo
    {
        uint64_t localStorage;
        uint64_t entry;
        uint64_t stack;
        sl::Atomic<uint64_t> booted;
        uint64_t cr3;
    };

    static sl::Span<uint64_t> savedMtrrs;
    static sl::Atomic<size_t> readyCpus;
    static sl::Atomic<bool> unleashAps;

    static void ApEntryFunc(uint64_t localStorage)
    {
        //setting cpu locals must be the first thing we do, since logging
        //relies on this for cpu-id
        auto* locals = reinterpret_cast<const CoreLocalHeader*>(localStorage);
        HwSetMyLocals(localStorage, locals->swId);
        Log("Core %zu is online.", LogLevel::Info, MyCoreId());

        RestoreMtrrs(savedMtrrs);
        CommonCpuSetup();

        CalibrateTsc();
        NPK_ASSERT(InitApLapic());

        ThreadContext idleContext {};
        BringCpuOnline(&idleContext);

        Log("AP init thread done, becoming idle thread.", LogLevel::Verbose);
        IntrsOn();

        readyCpus.Add(1, sl::Release);
        while (!unleashAps.Load(sl::Acquire))
            sl::HintSpinloop();

        while (true)
        {
            auto& dom = MySystemDomain();
            EnterNoEpochState(dom.rcu, MyRelativeCoreId());
            WaitForIntr();
            ExitNoEpochState(dom.rcu, MyRelativeCoreId());
        }
    }

    static bool TryStartAp(uint32_t lapicId, BootInfo* bootInfo, 
        sl::TimeCount deAssertDelay, sl::TimeCount sipiDelay)
    {
        Log("Sending INIT IPI to lapic-%u", LogLevel::Trace, lapicId);
        SendIpi(lapicId, IpiType::Init, 0);

        StallFor(deAssertDelay);
        NPK_CHECK(LastIpiSent(), false);

        Log("Sending INIT de-assert IPI to lapic-%u", LogLevel::Trace, lapicId);
        SendIpi(lapicId, IpiType::InitDeAssert, 0);

        NPK_CHECK(LastIpiSent(), false);

        for (size_t i = 0; i < 2; i++)
        {
            Log("Sending startup IPI %zu to lapic-%u", LogLevel::Trace, 
                i, lapicId);
            SendIpi(lapicId, IpiType::Startup, apBootPage >> PfnShift());

            StallFor(sipiDelay);
            NPK_CHECK(LastIpiSent(), false);

            if (bootInfo->booted.Load() == 1)
            {
                Log("Successfully started AP at lapic-%u", LogLevel::Verbose, 
                    lapicId);
                return true;
            }
        }

        Log("Failed to start AP at lapic-%u, no response.", LogLevel::Error, 
            lapicId);
        return false;
    }

    size_t HwBootAps(uintptr_t& virtBase, PerCpuData data)
    {
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

            SetKernelMap(virtBase, LookupPagePaddr(storage), VmFlag::Write);
            savedMtrrs = sl::Span<uint64_t>(reinterpret_cast<uint64_t*>(virtBase), mtrrCount);
            virtBase += PageSize();

            SaveMtrrs(savedMtrrs);
        }

        //we can only load 32-bits into cr3 from the spinup code
        NPK_ASSERT(apBootPage != Paddr());
        NPK_ASSERT(apBootPage < 1 * MiB);
        NPK_ASSERT(!savedMtrrs.Empty());

        auto maybeMadt = GetAcpiTable(sl::SigMadt);
        NPK_CHECK(maybeMadt.HasValue(), 0);

        //TODO: detect the default programmatically
        const bool modernDelays = 
            ReadConfigUint("npk.x86.lapic_modern_delays", false); 
        const auto initDeAssertDelay = modernDelays ? 0_ms : 10_ms;
        const auto sipiDelay = modernDelays ? 10_us : 300_us;

        //0x1X versions are the integrated lapics, which is all we support.
        NPK_ASSERT((MyLapicVersion() & 0xF0) == 0x10); 

        PageAccessRef bootPageRef = AccessPage(apBootPage);
        const size_t infoOffset = ((uintptr_t)_EndOfSpinupBlob 
            - (uintptr_t)SpinupBlob) - sizeof(BootInfo);

        BootInfo* bootInfo = reinterpret_cast<BootInfo*>(infoOffset +
            reinterpret_cast<uintptr_t>(bootPageRef.vaddr));

        bootInfo->entry = reinterpret_cast<uint64_t>(&ApEntryFunc);
        bootInfo->cr3 = reinterpret_cast<uint64_t>(kernelRoot);

        size_t idAlloc = 1; //id=0 is BSP (the currently executing core)
        unleashAps.Store(false, sl::Release);
        readyCpus.Store(1, sl::Release);

        auto madt = static_cast<const sl::Madt*>(*maybeMadt);
        for (auto source = sl::NextMadtSubtable(madt); source != nullptr; 
            source = sl::NextMadtSubtable(madt, source))
        {
            uint32_t targetLapicId = MyLapicId();

            if (!IsValidLapicEntry(&targetLapicId, *source))
                continue;
            if (targetLapicId == MyLapicId())
                continue;

            Log("Preparing to start AP at lapic-%u", LogLevel::Verbose, 
                targetLapicId);
            bootInfo->stack = data.stackStride * (idAlloc - 1) + data.apStacksBase;
            bootInfo->stack += KernelStackSize();
            bootInfo->stack -= 8;
            bootInfo->localStorage = data.localsStride * (idAlloc - 1) + data.localsBase;
            bootInfo->booted = 0;

            auto locals = 
                reinterpret_cast<CoreLocalHeader*>(bootInfo->localStorage);
            locals->swId = idAlloc;
            locals->selfAddr = bootInfo->localStorage;

            //compiler memory barrier: ensure all above memory ops dont move
            //beyond this point.
            asm("" ::: "memory");

            if (!TryStartAp(targetLapicId, bootInfo, initDeAssertDelay, 
                sipiDelay))
                continue;

            idAlloc++;
        }

        while (idAlloc > readyCpus.Load(sl::Acquire))
            sl::HintSpinloop();

        Log("AP startup done, %zu cpus running", LogLevel::Info, idAlloc);

        return idAlloc - 1;
    }

    void HwReleaseAps()
    {
        unleashAps.Store(true, sl::Release);
    }
}
