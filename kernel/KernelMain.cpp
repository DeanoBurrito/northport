#include <Log.h>
#include <Cpu.h>
#include <memory/PhysicalMemory.h>
#include <memory/Paging.h>
#include <memory/KernelHeap.h>
#include <memory/IpcManager.h>
#include <acpi/AcpiTables.h>
#include <devices/LApic.h>
#include <devices/IoApic.h>
#include <devices/8254Pit.h>
#include <devices/SystemClock.h>
#include <scheduling/Scheduler.h>
#include <filesystem/Vfs.h>
#include <arch/x86_64/Gdt.h>
#include <arch/x86_64/Idt.h>
#include <arch/x86_64/Tss.h>
#include <arch/x86_64/ApBoot.h>
#include <boot/Stivale2.h>
#include <Panic.h>

//needs to be implemented once per linked program. This is for the kernel.
sl::NativePtr currentProgramElf;
NativeUInt Kernel::vmaHighAddr;

//defined down below, forward declared for code-arranging purposes.
extern "C"
{
    [[gnu::used, noreturn]]
    void _ApEntry(Kernel::SmpCoreInfo* smpInfo);
}

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
        PageTableManager::Current()->InitKernel();
        PageTableManager::Current()->MakeActive();

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
        InitPit(0, INT_VECTOR_PIT_TICK);
        SetApicForUptime(false); //use PIT for uptime until first apic is initialized
        SetPitMasked(false);

        Filesystem::VFS::Global()->Init();
        Memory::IpcManager::Global()->Init();

        const uint32_t baseProcessorId = sl::MemRead<uint32_t>(EnsureHigherHalfAddr(CPU::ReadMsr(MSR_APIC_BASE) & ~(0xFFF)) + 0x20);
        Scheduling::Scheduler::Global()->Init(baseProcessorId);

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

    void SetupAllCores()
    {
        //we'll need the bsp setup properly in order to boot APs
        const uint32_t apicIdReg = sl::MemRead<uint32_t>(EnsureHigherHalfAddr(CPU::ReadMsr(MSR_APIC_BASE) & ~(0xFFF)) + 0x20);
        InitCore(apicIdReg >> 24, 0);

        SmpInfo* smpInfo = BootAPs();
        for (size_t i = 0; smpInfo->cores[i].apicId != AP_BOOT_APIC_ID_END; i++)
        {
            SmpCoreInfo* coreInfo = &smpInfo->cores[i];

            if (coreInfo->apicId == smpInfo->bspApicId)
            {
                GetCoreLocal()->acpiProcessorId = coreInfo->acpiProcessorId;
#ifdef NORTHPORT_DEBUG_DISABLE_SMP_BOOT
                Log("SMP support disabled at compile-time, running in single core mode.", LogSeverity::Info);
                break;
#else
                continue;
#endif
            }
#ifdef NORTHPORT_DEBUG_DISABLE_SMP_BOOT
            continue;
#endif
            //unused apic id, ignore it
            if (coreInfo->apicId == AP_BOOT_APIC_ID_INVALID)
                continue;

            Scheduling::Scheduler::Global()->AddProcessor(coreInfo->apicId);
            
            //allocate an init stack for this core and populate its info
            sl::NativePtr stackBase = Memory::PMM::Global()->AllocPage();
            Memory::PageTableManager::Current()->MapMemory(EnsureHigherHalfAddr(stackBase.raw), stackBase, Memory::MemoryMapFlags::AllowWrites);

            coreInfo->stack = EnsureHigherHalfAddr(stackBase.raw + PAGE_FRAME_SIZE);
            coreInfo->gotoAddress = (uint64_t)_ApEntry;
        }
    }

    [[noreturn]]
    void ExitInit()
    {
        CPU::SetInterruptsFlag();
        Devices::LApic::Local()->SetupTimer(SCHEDULER_TIMER_TICK_MS, INT_VECTOR_SCHEDULER_TICK, true);
        Logf("Core %lu init completed in: %lu ms. Exiting to scheduler ...", LogSeverity::Info, GetCoreLocal()->apicId, Devices::GetUptime());

        //NOTE: this time includes local apic timer calibration time (100ms)
        Scheduling::Scheduler::Global()->Yield();
        __builtin_unreachable();
    }
}

extern "C"
{
    [[gnu::used, noreturn]]
    void _ApEntry(Kernel::SmpCoreInfo* smpInfo)
    {
        using namespace Kernel;
        //ensure we're using our page map
        Memory::PageTableManager::Current()->MakeActive();
        smpInfo = EnsureHigherHalfAddr(smpInfo);

        InitCore(smpInfo->apicId, smpInfo->acpiProcessorId);

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
        //PageTableManager::Current() will return the current thread's page tables, or the init set if gs_base is null (i.e no core local yet)
        CPU::WriteMsr(MSR_GS_BASE, 0);

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
