#include <boot/CommonInit.h>
#include <boot/LimineTags.h>
#include <arch/Cpu.h>
#include <arch/Platform.h>
#include <config/ConfigStore.h>
#include <config/DeviceTree.h>
#include <config/AcpiTables.h>
#include <debug/Log.h>
#include <debug/TerminalDriver.h>
#include <debug/Symbols.h>
#include <debug/BakedConstants.h>
#include <drivers/DriverManager.h>
#include <drivers/ElfLoader.h>
#include <filesystem/Filesystem.h>
#include <interrupts/Ipi.h>
#include <interrupts/Router.h>
#include <io/IoManager.h>
#include <memory/Pmm.h>
#include <memory/Vmm.h>
#include <memory/Heap.h>
#include <tasking/Clock.h>
#include <tasking/Threads.h>
#include <tasking/Scheduler.h>
#include <NanoPrintf.h>
#include <UnitConverter.h>

namespace Npk
{
    uintptr_t hhdmBase;
    uintptr_t hhdmLength;
    sl::Atomic<size_t> bootloaderRefs;

    npk_device_desc* acpiRuntimeDescriptor;

    void ThreadedArchInit(); //defined in Init.cpp for the current architecture
    
    void InitEarlyPlatform()
    {
        using namespace Debug;
        Log("\r\nNorthport kernel %zu.%zu.%zu for %s started, based on commit %s, compiled by %s.", LogLevel::Info, 
            versionMajor, versionMinor, versionRev, targetArchStr, gitCommitShortHash, toolchainUsed);
        Config::InitConfigStore();
        Boot::CheckLimineTags();

        hhdmBase = Boot::hhdmRequest.response->offset;
        for (size_t i = Boot::memmapRequest.response->entry_count - 1; i > 0; i--)
        {
            auto entry = Boot::memmapRequest.response->entries[i];
            switch (entry->type)
            {
            case LIMINE_MEMMAP_USABLE:
            case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
            case LIMINE_MEMMAP_KERNEL_AND_MODULES:
            case LIMINE_MEMMAP_FRAMEBUFFER:
                break;
            default:
                continue;
            }

            hhdmLength = sl::AlignUp(entry->base + entry->length, PageSize);
            break;
        }
        Log("Hhdm: base=0x%tx, length=0x%tx", LogLevel::Info, hhdmBase, hhdmLength);

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
        Config::LateInitConfigStore();
        Debug::LoadKernelSymbols();

        using namespace Boot;
        if (framebufferRequest.response != nullptr)
            Debug::InitEarlyTerminals();
        else
            Log("Bootloader did not provide framebuffer.", LogLevel::Warning);

        acpiRuntimeDescriptor = nullptr;
        if (rsdpRequest.response != nullptr && rsdpRequest.response->address != nullptr)
        {
            Config::SetRsdp(SubHhdm(rsdpRequest.response->address));

            npk_load_name* loadName = new npk_load_name();
            loadName->type = npk_load_type::AcpiRuntime;
            loadName->length = 0;
            loadName->str = nullptr;

            auto initTag = new npk_init_tag_rsdp();
            initTag->rsdp = SubHhdm(rsdpRequest.response->address);
            initTag->header.type = npk_init_tag_type::Rsdp;

            acpiRuntimeDescriptor = new npk_device_desc();
            acpiRuntimeDescriptor->load_name_count = 1;
            acpiRuntimeDescriptor->load_names = loadName;
            acpiRuntimeDescriptor->friendly_name.data = "rsdp";
            acpiRuntimeDescriptor->friendly_name.length = 4;
            acpiRuntimeDescriptor->init_data = &initTag->header;
        }
        
        if (dtbRequest.response != nullptr && dtbRequest.response->dtb_ptr != nullptr)
            Config::DeviceTree::Global().Init(dtbRequest.response->dtb_ptr);

        ScanGlobalTopology();
        Drivers::DriverManager::Global().Init();
        Filesystem::InitVfs();

        Tasking::ProgramManager::Global().Init();
        Tasking::Scheduler::Global().Init();
        Io::IoManager::Global().Init();
    }

    void ReclaimMemoryThread(void*)
    {
        Boot::CheckLimineTags();
        PMM::Global().ReclaimBootMemory();
        Tasking::Thread::Current().Exit(0);
    }

    void InitThread(void*)
    {
        ThreadedArchInit();
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

                constexpr const char NameFormat[] = "ECAM segment %u (paddr=0x%" PRIx64"), busses %u-%u";
                const size_t friendlyNameLen = npf_snprintf(nullptr, 0, NameFormat, segment->id, 
                    segment->base, segment->firstBus, segment->lastBus);
                char* friendlyNameBuff = new char[friendlyNameLen + 1];
                npf_snprintf(friendlyNameBuff, friendlyNameLen + 1, NameFormat, segment->id, 
                    segment->base, segment->firstBus, segment->lastBus);
                descriptor->friendly_name.length = friendlyNameLen;
                descriptor->friendly_name.data = friendlyNameBuff;
                //NOTE: the descriptor owns the string buffer, so we're not leaking anything here.
                
                Drivers::DriverManager::Global().AddDescriptor(descriptor);
            }
        }

        if (acpiRuntimeDescriptor != nullptr)
            Drivers::DriverManager::Global().AddDescriptor(acpiRuntimeDescriptor);

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
        Interrupts::InterruptRouter::Global().InitCore();
    }

    [[noreturn]]
    void ExitCoreInit()
    {
        using namespace Tasking;
        Scheduler::Global().AddEngine();

        if (--bootloaderRefs == 0)
        {
            auto initThread = Thread::Create(Process::Kernel().Id(), ReclaimMemoryThread, nullptr);
            ASSERT_(initThread != nullptr);
            Log("Bootloader reclamation thread spawned, tid=%zu", LogLevel::Verbose, initThread->Id());
            initThread->Start(nullptr);
        }

        Scheduler::Global().StartEngine();
        ASSERT_UNREACHABLE();
    }
}
