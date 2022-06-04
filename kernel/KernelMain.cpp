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
#include <devices/Rtc.h>
#include <scheduling/Scheduler.h>
#include <filesystem/Vfs.h>
#include <arch/x86_64/Gdt.h>
#include <arch/x86_64/Idt.h>
#include <arch/x86_64/Tss.h>
#include <arch/x86_64/ApBoot.h>
#include <boot/Stivale2.h>
#include <InterruptManager.h>
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
        const stivale2_struct_tag_pmrs* pmrs = FindStivaleTag<stivale2_struct_tag_pmrs*>(STIVALE2_STRUCT_TAG_PMRS_ID);
        size_t tentativeHeapStart = 0;
        for (size_t i = 0; i < pmrs->entries; i++)
        {
            const stivale2_pmr* pmr = &pmrs->pmrs[i];
            const size_t pmrTop = pmr->base + pmr->length;

            if (pmrTop > tentativeHeapStart)
                tentativeHeapStart = pmrTop;
        }

        const sl::BufferView hhdm = PageTableManager::Current()->GetHhdm();
        KernelHeap::Global()->Init(hhdm.base.raw + hhdm.length + 2 * GB, false);
        Log("Memory init complete.", LogSeverity::Info);
    }

    //this function does nothing interesting, just prints info I find neat. Useful for remote debugging.
    void PrintBootAndMemoryInfo()
    {
        Log("Bootloader provided info:", LogSeverity::Verbose);

        Logf("Bootloader used: %s, %s", LogSeverity::Verbose, stivale2Struct->bootloader_brand, stivale2Struct->bootloader_version);

        const stivale2_struct_tag_memmap* mmap = FindStivaleTag<stivale2_struct_tag_memmap*>(STIVALE2_STRUCT_TAG_MEMMAP_ID);
        Logf("Memory regions: %lu", LogSeverity::Verbose, mmap->entries);
        for (size_t i = 0; i < mmap->entries; i++)
        {
            const stivale2_mmap_entry* entry = &mmap->memmap[i];
            const char* typeString = "unknown";

            switch (entry->type)
            {
                case STIVALE2_MMAP_USABLE: 
                    typeString = "usable"; break;
                case STIVALE2_MMAP_RESERVED: 
                    typeString = "reserved"; break;
                case STIVALE2_MMAP_ACPI_RECLAIMABLE: 
                    typeString = "acpi reclaim"; break;
                case STIVALE2_MMAP_ACPI_NVS: 
                    typeString = "acpi nvs"; break;
                case STIVALE2_MMAP_BAD_MEMORY: 
                    typeString = "bad"; break;
                case STIVALE2_MMAP_BOOTLOADER_RECLAIMABLE: 
                    typeString = "bootloader reclaim"; break;
                case STIVALE2_MMAP_KERNEL_AND_MODULES: 
                    typeString = "kernel/modules"; break;
                case STIVALE2_MMAP_FRAMEBUFFER: 
                    typeString = "boot framebuffer"; break;
            }

            Logf("region %u: base=0x%0lx, length=0x%0lx, type=%s", LogSeverity::Verbose, i, entry->base, entry->length, typeString);
        }

        const stivale2_struct_tag_framebuffer* stivaleFb = reinterpret_cast<stivale2_struct_tag_framebuffer*>(FindStivaleTagInternal(STIVALE2_STRUCT_TAG_FRAMEBUFFER_ID));
        if (stivaleFb != nullptr)
        {
            Logf("Bootloader framebuffer: w=%u, h=%u, stride=0x%x, bpp=%u", LogSeverity::Verbose, 
                stivaleFb->framebuffer_width, stivaleFb->framebuffer_height, stivaleFb->framebuffer_pitch, stivaleFb->framebuffer_bpp);
            Logf("Framebuffer colour format: r=%u bits << %u, g=%u bits << %u, b=%u bits << %u", LogSeverity::Verbose,
                stivaleFb->red_mask_size, stivaleFb->red_mask_shift,
                stivaleFb->green_mask_size, stivaleFb->green_mask_shift,
                stivaleFb->blue_mask_size, stivaleFb->blue_mask_shift);
        }
        else
            Log("No bootloader framebuffer detected.", LogSeverity::Verbose);

        const stivale2_struct_tag_edid* edid = reinterpret_cast<stivale2_struct_tag_edid*>(FindStivaleTagInternal(STIVALE2_STRUCT_TAG_EDID_ID));
        if (edid != nullptr)
            Log("EDID available.", LogSeverity::Verbose);

        const stivale2_struct_tag_epoch* epoch = reinterpret_cast<stivale2_struct_tag_epoch*>(FindStivaleTagInternal(STIVALE2_STRUCT_TAG_EPOCH_ID));
        if (epoch != nullptr)
            Logf("Epoch at boot: %lu", LogSeverity::Verbose, epoch->epoch);

        const stivale2_struct_tag_firmware* firmware = reinterpret_cast<stivale2_struct_tag_firmware*>(FindStivaleTagInternal(STIVALE2_STRUCT_TAG_FIRMWARE_ID));
        if (firmware != nullptr)
            Logf("Booted by BIOS (false means UEFI): %b", LogSeverity::Verbose, firmware->flags);
        
        const stivale2_struct_tag_smbios* smbios = reinterpret_cast<stivale2_struct_tag_smbios*>(FindStivaleTagInternal(STIVALE2_STRUCT_TAG_SMBIOS_ID));
        if (smbios != nullptr)
            Logf("SM BIOS entry points: 32bit=0x%x, 64bit=0x%lx", LogSeverity::Verbose, smbios->smbios_entry_32, smbios->smbios_entry_64);

        Log("End of bootloader info.", LogSeverity::Verbose);

        auto pmmStats = Memory::PMM::Global()->GetStats();
        Logf("Physical memory detected: %U (%u bytes)", LogSeverity::Info, pmmStats.totalPages * pmmStats.pageSizeInBytes, pmmStats.totalPages * pmmStats.pageSizeInBytes);
        Logf("Breakdown of physical memory: %U/%U in use, %U reserved, %U reclaimable, %U in use by kernel.", LogSeverity::Verbose, 
            pmmStats.usedPages * pmmStats.pageSizeInBytes, pmmStats.totalPages * pmmStats.pageSizeInBytes,
            pmmStats.reservedBytes, pmmStats.reclaimablePages * pmmStats.pageSizeInBytes, pmmStats.kernelPages * pmmStats.pageSizeInBytes);

        Memory::HeapMemoryStats heapStats;
        Memory::KernelHeap::Global()->GetStats(heapStats);
        Logf("KHeap init stats: slabsBase=0x%lx, poolBase=0x%lx", LogSeverity::Verbose, heapStats.slabsGlobalBase.raw, heapStats.slabsGlobalBase.raw + Memory::KernelHeapPoolOffset);
        Logf("Pool stats: %U/%U used, nodeCount=%u", LogSeverity::Verbose, heapStats.poolStats.usedBytes, heapStats.poolStats.totalSizeBytes, heapStats.poolStats.nodes);
        for (size_t i = 0; i < heapStats.slabCount; i++)
        {
            Logf("Slab %u stats: size=%u bytes, base=0x%lx, usage %u/%u slabs, segments=%u", LogSeverity::Verbose,
                i, heapStats.slabStats[i].slabSize, heapStats.slabStats[i].base.raw, 
                heapStats.slabStats[i].usedSlabs, heapStats.slabStats[i].totalSlabs,
                heapStats.slabStats[i].segments);
        }

        Log("End of memory info.", LogSeverity::Verbose);
    }

    void InitPlatform()
    {
        using namespace Devices;
        Log("Initializing platform ...", LogSeverity::Info);

        const stivale2_struct_tag_kernel_file_v2* kernelFile = FindStivaleTag<stivale2_struct_tag_kernel_file_v2*>(STIVALE2_STRUCT_TAG_KERNEL_FILE_V2_ID);
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
            Logf("Cpu freqs: coreBase=%uhz, coreMax=%uhz, bus=%uhz, tscTick=%uhz", LogSeverity::Verbose, 
                cpuFreqs.coreClockBaseHertz, cpuFreqs.coreMaxBaseHertz, cpuFreqs.busClockHertz, cpuFreqs.coreTimerHertz);

        //idt is shared across all cores, so we can set that up here
        SetupIDT();
        InterruptManager::Global()->Init();
        
        const stivale2_struct_tag_rsdp* stivaleRsdpTag = FindStivaleTag<stivale2_struct_tag_rsdp*>(STIVALE2_STRUCT_TAG_RSDP_ID);
        ACPI::AcpiTables::Global()->Init(stivaleRsdpTag->rsdp);

        InitPanic();
        IoApic::InitAll();

        SetBootEpoch(Devices::ReadRtcTime());
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
        const stivale2_struct_tag_hhdm* hhdm = FindStivaleTag<stivale2_struct_tag_hhdm*>(STIVALE2_STRUCT_TAG_HHDM_ID);
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
        PrintBootAndMemoryInfo(); //stuff we couldnt print until memory was setup

        InitPlatform();
        SetupAllCores();

        QueueInitTasks();
        ExitInit();
        __builtin_unreachable();
    }
}
