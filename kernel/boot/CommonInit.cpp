#include <boot/CommonInit.h>
#include <boot/LimineTags.h>
#include <arch/Cpu.h>
#include <arch/Platform.h>
#include <config/DeviceTree.h>
#include <config/AcpiTables.h>
#include <debug/Log.h>
#include <debug/TerminalDriver.h>
#include <debug/Symbols.h>
#include <debug/BakedConstants.h>
#include <drivers/DriverManager.h>
#include <drivers/ElfLoader.h>
#include <filesystem/Filesystem.h>
#include <interrupts/InterruptManager.h>
#include <interrupts/Ipi.h>
#include <memory/Pmm.h>
#include <memory/Vmm.h>
#include <memory/Heap.h>
#include <tasking/Clock.h>
#include <tasking/Scheduler.h>
#include <UnitConverter.h>

namespace Npk
{
    uintptr_t hhdmBase;
    uintptr_t hhdmLength;
    sl::Atomic<size_t> bootloaderRefs;
    
    void InitEarlyPlatform()
    {
        Log("\r\nNorthport kernel %lu.%lu.%lu for %s started.", LogLevel::Info, 
            Debug::versionMajor, Debug::versionMinor, Debug::versionRev, Debug::targetArchStr);
        Boot::CheckLimineTags();

        hhdmBase = Boot::hhdmRequest.response->offset;
        for (size_t i = Boot::memmapRequest.response->entry_count - 1; i > 0; i--)
        {
            auto entry = Boot::memmapRequest.response->entries[i];
            auto type = entry->type;
            if (type != LIMINE_MEMMAP_USABLE && type != LIMINE_MEMMAP_KERNEL_AND_MODULES &&
                type != LIMINE_MEMMAP_FRAMEBUFFER && type != LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE)
                continue;

            hhdmLength = sl::AlignUp(entry->base + entry->length, PageSize);
            break;
        }
        Log("Hhdm: base=0x%lx, length=0x%lx", LogLevel::Info, hhdmBase, hhdmLength);

        const auto loaderResp = Boot::bootloaderInfoRequest.response;
        if (loaderResp != nullptr)
            Log("Kernel loaded by: %s v%s", LogLevel::Info, loaderResp->name, loaderResp->version);

        if (Boot::smpRequest.response == nullptr)
            bootloaderRefs = 1;
        else
            bootloaderRefs = Boot::smpRequest.response->cpu_count;
    }

    void InitMemory()
    {
        PMM::Global().Init();
        VMM::InitKernel();
        Memory::Heap::Global().Init();
    }

    void InitPlatform()
    {
        Debug::LoadKernelSymbols();

        using namespace Boot;
        if (framebufferRequest.response != nullptr)
            Debug::InitEarlyTerminals();
        else
            Log("Bootloader did not provide framebuffer.", LogLevel::Warning);

        if (rsdpRequest.response != nullptr && rsdpRequest.response->address != nullptr)
            Config::SetRsdp(SubHhdm(rsdpRequest.response->address));
        else
            Log("Bootloader did not provide RSDP (or it was null).", LogLevel::Warning);
        
        if (dtbRequest.response != nullptr && dtbRequest.response->dtb_ptr != nullptr)
            Config::DeviceTree::Global().Init(dtbRequest.response->dtb_ptr);
        else
            Log("Bootloader did not provide DTB (or it was null).", LogLevel::Warning);

        ScanGlobalTopology();
        Drivers::DriverManager::Global().Init();
        Filesystem::InitVfs();

        Interrupts::InterruptManager::Global().Init();
        Tasking::Scheduler::Global().Init();
    }

    void ReclaimMemoryThread(void*)
    {
        //since the memory map is contained within the memory we're going to reclaim,
        //we'll need our own copy.
        limine_memmap_entry reclaimEntries[Boot::memmapRequest.response->entry_count];
        size_t reclaimCount = 0;
        size_t reclaimAmount = 0;

        for (size_t i = 0; i < Boot::memmapRequest.response->entry_count; i++)
        {
            if (Boot::memmapRequest.response->entries[i]->type != LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE)
                continue;
            reclaimEntries[reclaimCount++] = *Boot::memmapRequest.response->entries[i];
            reclaimAmount += reclaimEntries[reclaimCount - 1].length;
        }

        for (size_t i = 0; i < reclaimCount; i++)
            PMM::Global().IngestMemory(reclaimEntries[i].base, reclaimEntries[i].length);

        auto reclaimConv = sl::ConvertUnits(reclaimAmount, sl::UnitBase::Binary);
        Log("Reclaimed %lu.%lu %sB (%lu entries) of bootloader memory.", LogLevel::Info, 
            reclaimConv.major, reclaimConv.minor, reclaimConv.prefix, reclaimCount);

        Tasking::Thread::Current().Exit(0);
    }

    void InitThread(void*)
    {
        Drivers::ScanForModules("/initdisk/drivers/");

        //check for PCI controllers presenting themselves via MCFG
        using namespace Config;
        if (auto maybeMcfg = FindAcpiTable(SigMcfg); maybeMcfg.HasValue())
        {
            auto* mcfg = static_cast<const Mcfg*>(*maybeMcfg);
            const size_t segmentCount = (mcfg->length - sizeof(Mcfg)) / sizeof(McfgSegment);
            for (size_t i = 0; i < segmentCount; i++)
            {
                const McfgSegment* segment = &mcfg->segments[i];

                npk_load_name* loadName = new npk_load_name();
                loadName->length = 0;
                loadName->type = npk_load_type::PciHost;

                npk_init_tag_pci_host* initTag = new npk_init_tag_pci_host();
                initTag->header.type = npk_init_tag_type::PciHostAdaptor;
                initTag->type = npk_pci_host_type::Ecam;
                initTag->base_addr = segment->base;
                initTag->id = segment->id;
                initTag->first_bus = segment->firstBus;
                initTag->last_bus = segment->lastBus;

                npk_device_desc* descriptor = new npk_device_desc();
                descriptor->load_name_count = 1;
                descriptor->load_names = loadName;
                descriptor->init_data = &initTag->header;
                
                Drivers::DriverManager::Global().AddDescriptor(descriptor);
            }
        }
        //TODO: if x86, check for pci controller over port io

        Drivers::DriverManager::Global().PrintInfo();

        Tasking::Thread::Current().Exit(0);
    }

    bool CoresInEarlyInit()
    {
        return bootloaderRefs.Load(sl::Relaxed) != 0;
    }

    void PerCoreCommonInit()
    {
        //Memory::CreateLocalHeapCaches();
        Debug::InitCoreLogBuffers();
        Interrupts::InitIpiMailbox();

        //TODO: one core should start the system clock (StartSystemClock()), 
        //this should be determined by the platform init code, but done here.
        //The idea is to force the arch code to specify, make sure we dont forget to do it.
    }

    [[noreturn]]
    void ExitCoreInit()
    {
        using namespace Tasking;
        Thread* initThread = nullptr;
        if (bootloaderRefs != 0)
        {
            /* Cores can be initialized (and reach this function) in two ways: either by
             * the bootloader and are given to the kernel via the smp response, or hotplugged
             * at runtime. Bootloader started cores reference bootloader data (mainly the 
             * stack we're provided with) until they start scheduled execution - which presents a
             * problem for reclaiming bootloader memory.
             * The first if statement checks if *any* cores are still referencing bootloader memory,
             * and if spawning the reclamation thread should even be considered. For hotplugged cores
             * we dont care about reclaiming BL memory, so this filters out that case.
             * The second if (below) decrements the reference count, and checks the new value (atomically)
             * to see if this core is the last bootloader-started core. If it is, spawn the reclaim thread.
             */
            if (--bootloaderRefs == 0)
            {
                initThread = Scheduler::Global().CreateThread(ReclaimMemoryThread, nullptr);
                Log("Bootloader memory reclamation thread spawned, id=%lu.", LogLevel::Verbose, initThread->Id());
            }
        }
        else
            ASSERT_UNREACHABLE(); //TODO: start pluggable CPUs. (reclaim this core's boot data).

        Log("Core finished init, exiting to scheduler.", LogLevel::Info);
        Scheduler::Global().RegisterCore(initThread);
        ASSERT_UNREACHABLE();

    }
}
