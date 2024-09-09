#include <interfaces/loader/Generic.h>
#include <arch/Platform.h>
#include <arch/Init.h>
#include <arch/Timers.h>
#include <boot/CommonInit.h>
#include <config/ConfigStore.h>
#include <config/AcpiTables.h>
#include <config/DeviceTree.h>
#include <debug/BakedConstants.h>
#include <debug/TerminalDriver.h>
#include <debug/Log.h>
#include <debug/Symbols.h>
#include <debug/MagicKeys.h>
#include <drivers/DriverManager.h>
#include <filesystem/Filesystem.h>
#include <interrupts/Ipi.h>
#include <io/IntrRouter.h>
#include <memory/Pmm.h>
#include <memory/Vmm.h>
#include <memory/Heap.h>
#include <tasking/Clock.h>
#include <tasking/Scheduler.h>
#include <NanoPrintf.h>

namespace Npk
{
    uintptr_t hhdmBase;
    size_t hhdmLength;

    sl::Atomic<size_t> loaderDataRefs;

    bool CoresInEarlyInit()
    { return loaderDataRefs.Load(sl::Relaxed) != 0; }

    void ReclaimMemoryThread(void* arg)
    {
        PMM::Global().ReclaimBootMemory();
        Tasking::Thread::Current().Exit(0);
    }

    void InitThread(void* arg)
    {
        ArchThreadedInit();

        Drivers::ScanForModules("/initdisk/drivers");

        //add device descriptors for any pci segments we find via acpi
        using namespace Config;
        if (auto maybeMcfg = FindAcpiTable(SigMcfg); maybeMcfg.HasValue())
        {
            auto* mcfg = static_cast<const Mcfg*>(*maybeMcfg);
            const size_t segmentCount = (mcfg->length - sizeof(Mcfg)) / sizeof(McfgSegment);

            for (size_t i = 0; i < segmentCount; i++)
            {
                const McfgSegment* seg = &mcfg->segments[i];

                auto loadName = new npk_load_name();
                loadName->length = 0;
                loadName->type = npk_load_type::PciHost;

                auto initTag = new npk_init_tag_pci_host();
                initTag->header.type = npk_init_tag_type::PciHostAdaptor;
                initTag->type = npk_pci_host_type::Ecam;
                initTag->base_addr = seg->base;
                initTag->id = seg->id;
                initTag->first_bus = seg->firstBus;
                initTag->last_bus = seg->lastBus;

                auto descriptor = new npk_device_desc();
                descriptor->load_name_count = 1;
                descriptor->load_names = loadName;
                descriptor->init_data = &initTag->header;

                constexpr const char NameFormat[] = "ECAM segment %u (paddr=0x%" PRIx64"), busses %u-%u";
                const size_t nameLen = npf_snprintf(nullptr, 0, NameFormat, seg->id,
                    seg->base, seg->firstBus, seg->lastBus) + 1;
                char* nameBuf = new char[nameLen];
                npf_snprintf(nameBuf, nameLen, NameFormat, seg->id, seg->base, seg->firstBus, seg->lastBus);
                descriptor->friendly_name.length = nameLen;
                descriptor->friendly_name.data = nameBuf;
                //NOTE: the descriptor owns the friendly name string, so we're not leaking anything here.

                Drivers::DriverManager::Global().AddDescriptor(descriptor);
            }
        }

        //add device descriptor for rsdp if its available, so we can load an acpi runtime.
        if (auto rsdp = Config::GetRsdp(); rsdp.HasValue())
        {
            npk_load_name* loadName = new npk_load_name();
            loadName->type = npk_load_type::AcpiRuntime;
            loadName->length = 0;
            loadName->str = nullptr;

            auto initTag = new npk_init_tag_rsdp();
            initTag->rsdp = *rsdp;
            initTag->header.type = npk_init_tag_type::Rsdp;

            auto descriptor = new npk_device_desc();
            descriptor->load_name_count = 1;
            descriptor->load_names = loadName;
            descriptor->friendly_name.data = "rsdp";
            descriptor->friendly_name.length = 4;
            descriptor->init_data = &initTag->header;

            Drivers::DriverManager::Global().AddDescriptor(descriptor);
        }

        Drivers::DriverManager::Global().PrintInfo();
        Tasking::Thread::Current().Exit(0);
    }

    void PerCoreEntry(size_t myId)
    {
        Log("Core %zu has entered the kernel.", LogLevel::Info, myId);

        VMM::Kernel().MakeActive();
        ArchInitCore(myId);
        InitLocalTimers();

        //TODO: per-core heap caches
        Debug::InitCoreLogBuffers();
        Interrupts::InitIpiMailbox();
        Io::InterruptRouter::Global().InitCore();
        Tasking::Scheduler::Global().AddEngine();
    }

    [[noreturn]]
    void ExitCoreInit()
    {
        using namespace Tasking;
        if (--loaderDataRefs == 0)
        {
            auto reclaimThread = Thread::Create(Process::Kernel().Id(), ReclaimMemoryThread, nullptr);
            ASSERT_(reclaimThread != nullptr);
            Log("Bootloader reclaimation thread spawned: id=%zu", LogLevel::Info, reclaimThread->Id());
            reclaimThread->Start(nullptr);
        }

        Scheduler::Global().StartEngine();
        ASSERT_UNREACHABLE();
    }

    static void HandlePanicMagicKey(npk_key_id key)
    {
        (void)key;
        Log("Manually triggered via magic key combo.", LogLevel::Fatal);
    }

    extern "C" void KernelEntry()
    {
        loaderDataRefs = 1;
        ArchKernelEntry();

        using namespace Debug;
        Log("\r\nNorthport kernel %zu.%zu.%zu for %s started, based on commit %s, compiled by %s.", LogLevel::Info, 
            versionMajor, versionMinor, versionRev, targetArchStr, gitCommitShortHash, toolchainUsed);

        //get early access to config data, then validate what the bootloader gave us is what we expect.
        Config::InitConfigStore();
        ValidateLoaderData();

        ASSERT(GetHhdmBounds(hhdmBase, hhdmLength), "Boot protocol doesnt provide HHDM.");
        Log("Hhdm: base=0x%tx, length=0x%tx", LogLevel::Info, hhdmBase, hhdmLength);
        
        //init memory management stack
        PMM::Global().Init();
        VMM::InitKernel();
        Memory::Heap::Global().Init();

        if (auto rsdp = GetRsdp(); rsdp.HasValue())
            Config::SetRsdp(*rsdp);
        if (auto fdt = GetDtb(); fdt.HasValue()) //TODO: replace this junk with smoldtb
            Config::DeviceTree::Global().Init(*fdt);

        ArchLateKernelEntry();

        //set up other subsystems, now that the kernel heap is available
        AddMagicKey(npk_key_id_p, HandlePanicMagicKey);
        Config::LateInitConfigStore();
        LoadKernelSymbols();
        InitEarlyTerminals();

        Drivers::DriverManager::Global().Init();
        Filesystem::InitVfs();
        Tasking::ProgramManager::Global().Init();

        if ((loaderDataRefs = StartupAps()) == 1)
            Log("Boot protocol did not start APs, assuming uni-processor system for now.", LogLevel::Info);
        else
            Log("Boot protocol started %zu other cores.", LogLevel::Info, loaderDataRefs.Load() - 1);

        InitGlobalTimers();
        Tasking::StartSystemClock();
        ExitCoreInit();
    }
}
