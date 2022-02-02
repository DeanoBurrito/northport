#include <Log.h>
#include <Cpu.h>
#include <memory/PhysicalMemory.h>
#include <memory/Paging.h>
#include <memory/KernelHeap.h>
#include <acpi/AcpiTables.h>
#include <devices/LApic.h>
#include <devices/IoApic.h>
#include <devices/8254Pit.h>
#include <devices/SystemClock.h>
#include <devices/Keyboard.h>
#include <scheduling/Scheduler.h>
#include <arch/x86_64/Gdt.h>
#include <arch/x86_64/Idt.h>
#include <arch/x86_64/Tss.h>
#include <boot/Stivale2.h>
#include <Panic.h>

//needs to be implemented once per linked program. This is for the kernel.
sl::NativePtr currentProgramElf;
NativeUInt Kernel::vmaHighAddr;

namespace Kernel
{
    stivale2_struct* stivale2Struct;
    stivale2_tag* FindStivaleTagInternal(uint64_t id)
    {
        stivale2_tag* tag = sl::NativePtr(stivale2Struct->tags).As<stivale2_tag>();

        while (tag != nullptr)
        {
            if (tag->identifier == id)
                return tag;
            
            tag = sl::NativePtr(tag->next).As<stivale2_tag>();
        }

        return nullptr;
    }

    template<typename TagType>
    TagType FindStivaleTag(uint64_t id)
    {
        return reinterpret_cast<TagType>(FindStivaleTagInternal(id));
    }

    void InitMemory()
    {
        using namespace Memory;

        Log("Initializing memory ...", LogSeverity::Info);
        
        stivale2_struct_tag_memmap* mmap = FindStivaleTag<stivale2_struct_tag_memmap*>(STIVALE2_STRUCT_TAG_MEMMAP_ID);
        
        PMM::Global()->Init(mmap);
        PageTableManager::Setup();
        PageTableManager::Local()->InitKernel();
        PageTableManager::Local()->MakeActive();

        //assign heap to start immediately after last mapped kernel page
        stivale2_struct_tag_pmrs* pmrs = FindStivaleTag<stivale2_struct_tag_pmrs*>(STIVALE2_STRUCT_TAG_PMRS_ID);
        size_t tentativeHeapStart = 0;
        for (size_t i = 0; i < pmrs->entries; i++)
        {
            stivale2_pmr* pmr = &pmrs->pmrs[i];
            const size_t pmrTop = pmr->base + pmr->length;

            if (pmrTop > tentativeHeapStart)
                tentativeHeapStart = pmrTop;
        }

        KernelHeap::Global()->Init(tentativeHeapStart);
        Log("Memory init complete.", LogSeverity::Info);
    }

    void InitPlatform()
    {
        using namespace Devices;
        Log("Initializing platform ...", LogSeverity::Info);

        stivale2_struct_tag_kernel_file_v2* kernelFile = FindStivaleTag<stivale2_struct_tag_kernel_file_v2*>(STIVALE2_STRUCT_TAG_KERNEL_FILE_V2_ID);
        if (kernelFile == nullptr)
        {
            currentProgramElf.ptr = nullptr;
            Log("Unable to get kernel elf from bootloader, debug symbols unavailable.", LogSeverity::Error);
        }
        else
        {
            currentProgramElf.raw = kernelFile->kernel_file;
            Logf("Kernel elf located at 0x%lx", LogSeverity::Info, kernelFile->kernel_file);
        }

        CpuFrequencies cpuFreqs = CPU::GetFrequencies();
        if (cpuFreqs.coreTimerHertz == (uint32_t)-1)
            Log("Cpu freqs: unavilable, cpuid did not allow discovery.", LogSeverity::Verbose);
        else
            Logf("Cpu freqs: coreBase=%uhz, coreMax=%uhz, bus=%uhz, tscTick=%uhz", LogSeverity::Verbose, cpuFreqs.coreClockBaseHertz, cpuFreqs.coreMaxBaseHertz, cpuFreqs.busClockHertz, cpuFreqs.coreTimerHertz);

        //idt is shared across all cores, so we can set that up here
        SetupIDT();
        
        stivale2_struct_tag_rsdp* stivaleRsdpTag = FindStivaleTag<stivale2_struct_tag_rsdp*>(STIVALE2_STRUCT_TAG_RSDP_ID);
        ACPI::AcpiTables::Global()->Init(stivaleRsdpTag->rsdp);

        InitPanic();
        IoApic::InitAll();
        InitPit(0, INTERRUPT_GSI_PIT_TICK);
        SetApicForUptime(false); //use PIT for uptime until first apic is initialized
        SetPitMasked(false);

        Keyboard::Global()->Init();

        stivale2_struct_tag_smp* stivaleSmpTag = FindStivaleTag<stivale2_struct_tag_smp*>(STIVALE2_STRUCT_TAG_SMP_ID);
        if (!stivaleSmpTag)
            Scheduling::Scheduler::Global()->Init(1);
        else
            Scheduling::Scheduler::Global()->Init(stivaleSmpTag->cpu_count);

        Log("Platform init complete.", LogSeverity::Info);
    }

    void InitCore(size_t apicId, size_t acpiProcessorId)
    {
        CoreLocalStorage* coreStore = new CoreLocalStorage();
        coreStore->apicId = apicId;
        coreStore->acpiProcessorId = acpiProcessorId;
        coreStore->ptrs[CoreLocalIndices::LAPIC] = new Devices::LApic();
        coreStore->ptrs[CoreLocalIndices::TSS] = new TaskStateSegment();
        coreStore->ptrs[CoreLocalIndices::CurrentThread] = nullptr;

        FlushGDT();
        CPU::WriteMsr(MSR_GS_BASE, (size_t)coreStore);

        CPU::SetupExtendedState();
        LoadIDT();
        FlushTSS();
        Logf("Core %lu has setup core (GDT, IDT, TSS) and extended state.", LogSeverity::Info, apicId);

        Devices::LApic::Local()->Init();
        Logf("Core %lu LAPIC initialized.", LogSeverity::Verbose, apicId);
    }

    [[gnu::used, noreturn]]
    extern "C" void _ApEntry(stivale2_smp_info* smpInfo);

    void SetupAllCores()
    {
        stivale2_struct_tag_smp* smpTag = FindStivaleTag<stivale2_struct_tag_smp*>(STIVALE2_STRUCT_TAG_SMP_ID);
#ifdef NORTHPORT_DEBUG_DISABLE_SMP_BOOT
        if (true)
#else
        if (smpTag == nullptr)
#endif
        {
            Log("SMP not available on this system.", LogSeverity::Info);

            //manually read the apic id, since LApic class isnt initialized yet
            uint32_t apicIdReg = sl::MemRead<uint32_t>(EnsureHigherHalfAddr(CPU::ReadMsr(MSR_APIC_BASE) & ~(0xFFF)) + 0x20);
            InitCore(0, 0);
            return;
        }

        for (size_t i = 0; i < smpTag->cpu_count; i++)
        {
            if (smpTag->smp_info[i].lapic_id == smpTag->bsp_lapic_id)
            {
                InitCore(smpTag->smp_info[i].lapic_id, smpTag->smp_info[i].processor_id);
                continue;
            }

            sl::NativePtr stackBase = Memory::PMM::Global()->AllocPage();
            Memory::PageTableManager::Local()->MapMemory(EnsureHigherHalfAddr(stackBase.raw), stackBase, Memory::MemoryMapFlag::AllowWrites);

            smpTag->smp_info[i].target_stack = EnsureHigherHalfAddr(stackBase.raw) + PAGE_FRAME_SIZE;
            smpTag->smp_info[i].goto_address = (uint64_t)_ApEntry;
        }
    }

    [[noreturn]]
    void ExitInit()
    {
        CPU::SetInterruptsFlag();
        Devices::LApic::Local()->SetupTimer(SCHEDULER_TIMER_TICK_MS, INTERRUPT_GSI_SCHEDULER_NEXT, true);
        Logf("Core %lu init completed in: %lu ms. Exiting to scheduler ...", LogSeverity::Info, GetCoreLocal()->apicId, Devices::GetUptime());

        //NOTE: this time includes local apic timer calibration time (100ms)
        Scheduling::Scheduler::Global()->Yield();
        __builtin_unreachable();
    }
}

extern "C"
{
    [[gnu::used, noreturn]]
    void _ApEntry(stivale2_smp_info* smpInfo)
    {
        using namespace Kernel;
        //ensure we're using our page map
        Memory::PageTableManager::Local()->MakeActive();
        smpInfo = EnsureHigherHalfAddr(smpInfo);

        InitCore(smpInfo->lapic_id, smpInfo->processor_id);

        ExitInit();
        __builtin_unreachable();
    }
    
    extern void QueueInitTasks();

    [[gnu::used, noreturn]]
    void _KernelEntry(stivale2_struct* stivaleStruct)
    {
        using namespace Kernel;

        //setup static data: phys mem mirror addr, stivale struct
        stivale2Struct = stivaleStruct;
        stivale2_struct_tag_hhdm* hhdm = FindStivaleTag<stivale2_struct_tag_hhdm*>(STIVALE2_STRUCT_TAG_HHDM_ID);
        vmaHighAddr = hhdm->addr;

        CPU::ClearInterruptsFlag();
        CPU::DoCpuId();

        LoggingInitEarly();
#ifdef NORTHPORT_ENABLE_DEBUGCON_LOG_AT_BOOT
        EnableLogDestinaton(LogDestination::DebugCon);
#endif

        Log("", LogSeverity::EnumCount); //log empty line so the output of debugcon/serial is starting in a known place.
        Log("Northport kernel succesfully started.", LogSeverity::Info);

        InitMemory();
        LoggingInitFull();

        InitPlatform();
        SetupAllCores();

        QueueInitTasks();
        ExitInit();
        __builtin_unreachable();
    }
}
