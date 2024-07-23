#include <interfaces/loader/Generic.h>
#include <arch/Platform.h>
#include <arch/Init.h>
#include <arch/Timers.h>
#include <boot/CommonInit.h>
#include <config/ConfigStore.h>
#include <config/AcpiTables.h>
#include <debug/BakedConstants.h>
#include <debug/Log.h>
#include <debug/Symbols.h>
#include <drivers/DriverManager.h>
#include <filesystem/Filesystem.h>
#include <interrupts/Ipi.h>
#include <interrupts/Router.h>
#include <memory/Pmm.h>
#include <memory/Vmm.h>
#include <memory/Heap.h>
#include <tasking/Clock.h>
#include <tasking/Scheduler.h>

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

        //TODO: scan for loadable modules, add pci descriptors, add acpi runtime descriptor
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
        Interrupts::InterruptRouter::Global().InitCore();

        if (myId != 0)
            Halt();
    }

    [[noreturn]]
    static void ExitCoreInit()
    {
        using namespace Tasking;
        Scheduler::Global().AddEngine();

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
        //TODO: init device tree parser

        ArchLateKernelEntry();

        //set up other subsystems, now that the kernel heap is available
        Config::LateInitConfigStore();
        LoadKernelSymbols();
        //TODO: early terms

        Drivers::DriverManager::Global().Init();
        //TODO: attach rsdp to tree for acpi runtime driver
        Filesystem::InitVfs();
        Tasking::ProgramManager::Global().Init();

        if ((loaderDataRefs = StartupAps()) == 1)
            Log("Boot protocol did not start APs, assuming uni-processor system for now.", LogLevel::Info);
        else
            Log("Boot protocol started %zu other cores.", LogLevel::Info, loaderDataRefs.Load());

        InitGlobalTimers();
        Tasking::StartSystemClock();
        ExitCoreInit();
    }
}
