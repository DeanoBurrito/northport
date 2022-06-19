#include <Log.h>
#include <devices/LApic.h>
#include <devices/IoApic.h>
#include <devices/8254Pit.h>
#include <devices/Rtc.h>
#include <devices/SystemClock.h>
#include <arch/x86_64/Gdt.h>
#include <arch/x86_64/Idt.h>
#include <arch/x86_64/Tss.h>
#include <scheduling/Scheduler.h>

namespace Kernel::Boot
{
    size_t InitPlatformArch()
    {
        using namespace Devices;
        
        //let's do this once, since the IDT is shared between all cores.
        SetupIDT();
        IoApic::InitAll();

        SetBootEpoch(ReadRtcTime());
        InitPit(0, INT_VECTOR_PIT_TICK);
        SetApicForUptime(false);
        SetPitMasked(false);

        //use the lapic id as the core id
        return sl::MemRead<uint32_t>(EnsureHigherHalfAddr(CPU::ReadMsr(MSR_APIC_BASE) & ~(0xFFF)) + 0x20);
    }

    void InitCore(size_t id, size_t acpiId)
    {
        CoreLocalStorage* coreStore = new CoreLocalStorage();
        coreStore->apicId = id;
        coreStore->acpiProcessorId = acpiId;
        coreStore->ptrs[CoreLocalIndices::LAPIC] = new Devices::LApic();
        coreStore->ptrs[CoreLocalIndices::TSS] = new TaskStateSegment();
        coreStore->ptrs[CoreLocalIndices::CurrentThread] = nullptr;

        //we'll want to enable wp/umip/global pages, and smep/smap if available
        uint64_t cr0 = ReadCR0();
        cr0 |= (1 << 16); //enable write-protect for supervisor mode accessess
        WriteCR0(cr0);

        uint64_t cr4 = ReadCR4();
        if (CPU::FeatureSupported(CpuFeature::SMAP))
            cr4 |= (1 << 21);
        if (CPU::FeatureSupported(CpuFeature::SMEP))
            cr4 |= (1 << 20);
        if (CPU::FeatureSupported(CpuFeature::UMIP))
            cr4 |= (1 << 11);
        if (CPU::FeatureSupported(CpuFeature::GlobalPages))
            cr4 |= (1 << 7); 
        WriteCR4(cr4);
        CPU::AllowSumac(false);

        FlushGDT();
        CPU::WriteMsr(MSR_GS_BASE, (size_t)coreStore);

        CPU::SetupExtendedState();
        LoadIDT();
        FlushTSS();
        Logf("Core %lu has setup core (GDT, IDT, TSS) and extended state.", LogSeverity::Info, id);

        Devices::LApic::Local()->Init();
        Logf("Core %lu LAPIC initialized.", LogSeverity::Verbose, id);
    }

    [[noreturn]]
    void ExitInitArch()
    {
        CPU::SetInterruptsFlag();
        Devices::LApic::Local()->SetupTimer(SCHEDULER_TIMER_TICK_MS, INT_VECTOR_SCHEDULER_TICK, true);
        Logf("Core %lu init completed in: %lu ms. Exiting to scheduler ...", LogSeverity::Info, GetCoreLocal()->apicId, Devices::GetUptime());

        Scheduling::Scheduler::Global()->Yield();
        __builtin_unreachable();
    }
}