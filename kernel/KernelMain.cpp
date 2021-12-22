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
#include <arch/x86_64/Gdt.h>
#include <arch/x86_64/Idt.h>
#include <boot/Stivale2.h>
#include <StackTrace.h>

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
        PageTableManager::Local()->Init(true); //TODO: move away from bootloader map, and use our own
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
        Log("Initializing platform ...", LogSeverity::Info);

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
        Devices::SimpleFramebuffer::Global()->Init(framebufferTag);
        Devices::SimpleFramebuffer::Global()->DrawTestPattern();
#ifdef NORTHPORT_ENABLE_FRAMEBUFFER_LOG_AT_BOOT 
        EnableLogDestinaton(LogDestination::FramebufferOverwrite);
#endif

        Devices::IoApic::InitAll();

        size_t ps2PortCount = Devices::Ps2Controller::InitController();
        if (ps2PortCount > 0)
            Devices::Ps2Controller::Keyboard()->Init(false);
        if (ps2PortCount > 1)
            Devices::Ps2Controller::Mouse()->Init(true);

        Log("Platform init complete.", LogSeverity::Info);
    }

    CoreLocalStorage* PrepareCoreLocal(uint64_t apicId, uint64_t acpiId)
    {
        CoreLocalStorage* coreStore = new CoreLocalStorage();
        coreStore->apicId = apicId;
        coreStore->acpiProcessorId = acpiId;
        coreStore->ptrs[CoreLocalIndices::LAPIC] = new Devices::LApic();

        Logf("Core local storage created for core %u", LogSeverity::Verbose, apicId);
        return coreStore;
    }

    void InitCoreLocal()
    {
        Logf("Setting up core %u", LogSeverity::Info, GetCoreLocal()->apicId);
        
        uint64_t backupCoreLocal = CPU::ReadMsr(MSR_GS_BASE);
        FlushGDT();
        CPU::WriteMsr(MSR_GS_BASE, backupCoreLocal);
        Log("Core local GDT installed.", LogSeverity::Verbose);

        LoadIDT();
        Log("Core local IDT installed.", LogSeverity::Verbose);

        Devices::LApic::Local()->Init();
        Log("Local APIC initialized.", LogSeverity::Verbose);

        Log("Core specific setup complete.", LogSeverity::Info);
    }

    //the entry point for the APs
    [[gnu::used]]
    void _APEntry(uint64_t arg)
    {
        CPU::WriteMsr(MSR_GS_BASE, arg);
        InitCoreLocal();

        Log("AP has finished init, busy waiting.", LogSeverity::Info);
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
                continue;
            }

            sl::NativePtr stackBase = Memory::PMM::Global()->AllocPage();
            Memory::PageTableManager::Local()->MapMemory(stackBase, stackBase, Memory::MemoryMapFlag::AllowWrites);
            
            smpTag->smp_info[i].target_stack = stackBase.raw + PAGE_FRAME_SIZE;
            smpTag->smp_info[i].extra_argument = (uint64_t)coreStore;
            smpTag->smp_info[i].goto_address = (uint64_t)_APEntry;
        }
    }

    void ExitInit()
    {
        CPU::SetInterruptsFlag();
        Log("Kernel init done.", LogSeverity::Info);
    }
}

extern "C"
{
    [[noreturn]]
    void _KernelEntry(stivale2_struct* stivaleStruct)
    {
        using namespace Kernel;
        stivale2Struct = stivaleStruct;

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
        asm("int $0x01");

        ExitInit();
        for (;;)
            asm("hlt");
    }
}
