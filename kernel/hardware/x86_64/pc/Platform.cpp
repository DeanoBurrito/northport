#include <hardware/Plat.hpp>
#include <hardware/x86_64/Mmu.hpp>
#include <hardware/x86_64/LocalApic.hpp>
#include <AcpiTypes.hpp>
#include <KernelApi.hpp>

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
        uint32_t cr3;
    };

    void PlatInitEarly()
    {} //no-op

    size_t PlatGetCpuCount(InitState& state)
    {
        //TODO: find and parse madt early on, so we can get a total cpu count
        return 4;
    }

    void PlatInitDomain0(InitState& state)
    { (void)state; } //no-op

    void PlatInitFull(uintptr_t& virtBase)
    { (void)virtBase; }

    static void ApEntryFunc(uint64_t localStorage)
    {
        NPK_UNREACHABLE();
    }

    void PlatBootAps(uintptr_t stacks, uintptr_t perCpuStores, size_t perCpuStride)
    {
        NPK_ASSERT(apBootPage != Paddr());
        NPK_ASSERT(kernelMap >> 32 == 0); //we can only load 32-bits into cr3 from the spinup code

        auto maybeMadt = GetAcpiTable(SigMadt);
        NPK_CHECK(maybeMadt.HasValue(), );

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

            Log("Trying to start AP at lapic-%u", LogLevel::Trace, targetLapicId);
            bootInfo->stack = stacks + idAlloc * stackStride;
            bootInfo->localStorage = perCpuStores + idAlloc * perCpuStride;
            bootInfo->booted = 0;

            auto locals = reinterpret_cast<CoreLocalHeader*>(bootInfo->localStorage);
            locals->swId = idAlloc;
            locals->selfAddr = bootInfo->localStorage;

            //compiler memory barrier: ensure all above memory ops have dont move beyond this point
            asm("" ::: "memory");

            //send init ipi and wait 10ms
            SendIpi(targetLapicId, IpiType::Init, 0);
            auto startTime = PlatReadTimestamp();
            auto endTime = startTime.epoch + (10_ms).Rebase(startTime.Frequency).ticks;
            while (startTime.epoch < endTime)
                sl::HintSpinloop();

            if (!LastIpiSent())
            {
                Log("Failed to send INIT IPI to AP %u", LogLevel::Error, targetLapicId);
                continue;
            }

            //send init level de-assert, ignored on most CPUs - some *very* old ones require it
            SendIpi(targetLapicId, IpiType::InitDeAssert, 0);
            if (!LastIpiSent())
            {
                Log("Failed to send INIT de-assert IPI to AP %u", LogLevel::Error, targetLapicId);
                continue;
            }

            //send startup ipi
            SendIpi(targetLapicId, IpiType::Startup, apBootPage >> 12);
            startTime = PlatReadTimestamp();
            endTime = startTime.epoch + (200_us).Rebase(startTime.Frequency).ticks;
            while (startTime.epoch < endTime)
                sl::HintSpinloop();

            if (!LastIpiSent())
            {
                Log("Failed to send first SIPI to AP %u", LogLevel::Error, targetLapicId);
                continue;
            }

            //if cpu hasn't booted, send a second startup ipi (again required for really old cpus
            //we'll likely never see).
            if (bootInfo->booted.Load() == 0)
            {
                SendIpi(targetLapicId, IpiType::Startup, apBootPage >> 12);
                startTime = PlatReadTimestamp();
                endTime = startTime.epoch + (200_us).Rebase(startTime.Frequency).ticks;
                while (startTime.epoch < endTime)
                    sl::HintSpinloop();

                if (!LastIpiSent())
                {
                    Log("Failed to send second SIPI to AP %u", LogLevel::Error, targetLapicId);
                    continue;
                }
            }

            if (bootInfo->booted.Load() != 1)
            {
                Log("Failed to start AP at lapic-%u, no response.", LogLevel::Error, targetLapicId);
                continue;
            }

            Log("Started AP %zu at lapic-%u", LogLevel::Trace, idAlloc, targetLapicId);
            idAlloc++;
            //TODO: sync MTRRs, APs should init cr0/cr4/efer themselves (in ArchInit()?)
        }

        Log("AP startup done, %zu cpus running", LogLevel::Info, idAlloc);
    }

    void PlatSetAlarm(sl::TimePoint expiry)
    {
        NPK_UNREACHABLE();
    }

    sl::TimePoint PlatReadTimestamp()
    {
        NPK_UNREACHABLE();
    }
}
