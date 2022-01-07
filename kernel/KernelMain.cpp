#include <Log.h>
#include <Cpu.h>
#include <memory/PhysicalMemory.h>
#include <memory/Paging.h>
#include <memory/KernelHeap.h>
#include <acpi/AcpiTables.h>
#include <devices/LApic.h>
#include <devices/IoApic.h>
#include <devices/SimpleFramebuffer.h>
#include <devices/Ps2Controller.h>
#include <devices/Keyboard.h>
#include <devices/8254Pit.h>
#include <devices/SystemClock.h>
#include <scheduling/Scheduler.h>
#include <arch/x86_64/Gdt.h>
#include <arch/x86_64/Idt.h>
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
        PMM::Global()->InitLate();

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

        //we're sharing gdt and idt instances between cores, so we can set those up here.
        SetupGDT();
        SetupIDT();

        stivale2_struct_tag_rsdp* stivaleRsdpTag = FindStivaleTag<stivale2_struct_tag_rsdp*>(STIVALE2_STRUCT_TAG_RSDP_ID);
        ACPI::AcpiTables::Global()->Init(stivaleRsdpTag->rsdp);

        stivale2_struct_tag_framebuffer* framebufferTag = FindStivaleTag<stivale2_struct_tag_framebuffer*>(STIVALE2_STRUCT_TAG_FRAMEBUFFER_ID);
        SimpleFramebuffer::Global()->Init(framebufferTag);
        SimpleFramebuffer::Global()->DrawTestPattern();
#ifdef NORTHPORT_ENABLE_FRAMEBUFFER_LOG_AT_BOOT 
        EnableLogDestinaton(LogDestination::FramebufferOverwrite);
#endif
        InitPanic(); //framebuffer is needed for panic subsystem, so we init it here.
        IoApic::InitAll();
        InitPit(0, INTERRUPT_GSI_PIT_TICK);
        SetPitMasked(false); //start keeping track of uptime

        Keyboard::Global()->Init();
        size_t ps2PortCount = Ps2Controller::InitController();
        if (ps2PortCount > 0)
            Ps2Controller::Keyboard()->Init(false);
        if (ps2PortCount > 1)
            Ps2Controller::Mouse()->Init(true);


        Log("Platform init complete.", LogSeverity::Info);
    }

    CoreLocalStorage* PrepareCoreLocal(uint64_t apicId, uint64_t acpiId)
    {
        CoreLocalStorage* coreStore = new CoreLocalStorage();
        coreStore->apicId = apicId;
        coreStore->acpiProcessorId = acpiId;
        coreStore->ptrs[CoreLocalIndices::LAPIC] = new Devices::LApic();
        coreStore->ptrs[CoreLocalIndices::Scheduler] = new Scheduling::Scheduler();

        Logf("Core local storage created for core %lu", LogSeverity::Verbose, apicId);
        return coreStore;
    }

    void InitCoreLocal()
    {
        size_t coreNumber = GetCoreLocal()->apicId;
        uint64_t backupCoreLocal = CPU::ReadMsr(MSR_GS_BASE);
        Logf("Setting up core %u, coreLocalStorage=0x%lx", LogSeverity::Info, coreNumber, backupCoreLocal);

        CPU::SetupExtendedState(); //setup fpu, sse, avx if supported. Not used in kernel, but hardware is now ready
        FlushGDT();
        CPU::WriteMsr(MSR_GS_BASE, backupCoreLocal); //since reloading the gdt will flush the previous GS_BASE
        LoadIDT();
        Logf("Core %lu IDT and GDT installed.", LogSeverity::Verbose, coreNumber);

        Devices::LApic::Local()->Init();
        Logf("Core %lu Local APIC initialized.", LogSeverity::Verbose, coreNumber);

        Scheduling::Scheduler::Local()->Init();
        Logf("Core %lu scheduler initialized.", LogSeverity::Verbose, coreNumber);
    }

    //the entry point for the APs
    [[gnu::used]]
    void _APEntry(stivale2_smp_info* coreInfo)
    {
        Memory::PageTableManager::Local()->MakeActive(); //switch cpu to our page table

        coreInfo = EnsureHigherHalfAddr(coreInfo);
        CPU::WriteMsr(MSR_GS_BASE, coreInfo->extra_argument);
        InitCoreLocal();

        Logf("AP (coreId=%lu) has finished init, busy waiting.", LogSeverity::Info, GetCoreLocal()->apicId);
        while (true)
            asm("hlt");
    }

    void StartupAPs()
    {
        stivale2_struct_tag_smp* smpTag = FindStivaleTag<stivale2_struct_tag_smp*>(STIVALE2_STRUCT_TAG_SMP_ID);
        if (smpTag == nullptr)
        {
            Log("Couldn't get smp info from bootloader, aborting AP init.", LogSeverity::Info);
            return;
        }

#ifdef NORTHPORT_DEBUG_DISABLE_SMP_BOOT
#pragma message "AP init at boot disabled by define. Compiled for single-core only."
        Log("Kernel was compiled to only enable BSP, ignoring other cores.", LogSeverity::Info);
        CoreLocalStorage* coreStore = PrepareCoreLocal(smpTag->bsp_lapic_id, 0);
        CPU::WriteMsr(MSR_GS_BASE, (uint64_t)coreStore);
        return;
#endif

        //this has the side effect of setting up core info for bsp
        for (size_t i = 0; i < smpTag->cpu_count; i++)
        {
            CoreLocalStorage* coreStore = PrepareCoreLocal(smpTag->smp_info[i].lapic_id, smpTag->smp_info[i].processor_id);
            if (smpTag->smp_info[i].lapic_id == smpTag->bsp_lapic_id)
            {
                //stash the address, and resume the regular init path
                CPU::WriteMsr(MSR_GS_BASE, (uint64_t)coreStore);
                Logf("BSP has coreId=%lu", LogSeverity::Verbose, smpTag->bsp_lapic_id);
                continue;
            }

            sl::NativePtr stackBase = Memory::PMM::Global()->AllocPage();
            sl::NativePtr stackBaseHigh = EnsureHigherHalfAddr(stackBase.ptr);
            Memory::PageTableManager::Local()->MapMemory(stackBaseHigh, stackBase, Memory::MemoryMapFlag::AllowWrites);
            
            smpTag->smp_info[i].target_stack = stackBaseHigh.raw + PAGE_FRAME_SIZE;
            smpTag->smp_info[i].extra_argument = (uint64_t)EnsureHigherHalfAddr(coreStore);
            smpTag->smp_info[i].goto_address = (uint64_t)_APEntry;
        }
    }

    void ExitInit()
    {
        CPU::SetInterruptsFlag();

        Devices::LApic::Local()->SetupTimer(SCHEDULER_QUANTUM_MS, INTERRUPT_GSI_SCHEDULER_NEXT, true);
        Devices::SetPitMasked(true); //ensure PIT is masked here, lapic timer will progress uptime from now on.

        Logf("Boot completed in: %u ms", LogSeverity::Verbose, Devices::GetUptime()); //NOTE: this time includes local apic timer calibration time (100ms)
        Log("Kernel init done, exiting to scheduler.", LogSeverity::Info);
        Scheduling::Scheduler::Local()->Yield();
    }
}

extern "C"
{
    [[noreturn]]
    void _KernelEntry(stivale2_struct* stivaleStruct)
    {
        using namespace Kernel;

        //setup vma high offset, and move address of stivale base struct there.
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

        //init bsp
        StartupAPs();
        InitCoreLocal();

        ExitInit();

        Log("Kernel has somehow returned to pre-scheduled main, this should not happen.", LogSeverity::Fatal);
        for (;;)
            asm("hlt");
    }
}
