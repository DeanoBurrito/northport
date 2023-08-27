#include <boot/CommonInit.h>
#include <boot/LimineTags.h>
#include <arch/Cpu.h>
#include <arch/Platform.h>
#include <config/DeviceTree.h>
#include <config/AcpiTables.h>
#include <debug/Log.h>
#include <debug/NanoPrintf.h>
#include <debug/TerminalDriver.h>
#include <devices/DeviceManager.h>
#include <devices/PciBridge.h>
#include <drivers/DriverManager.h>
#include <filesystem/Filesystem.h>
#include <filesystem/FileCache.h>
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
        Boot::CheckLimineTags();

        hhdmBase = Boot::hhdmRequest.response->offset;
        auto lastEntry = Boot::memmapRequest.response->entries[Boot::memmapRequest.response->entry_count - 1];
        hhdmLength = sl::AlignUp(lastEntry->base + lastEntry->length, PageSize);
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
    }

    void InitPlatform()
    {
        using namespace Boot;
        if (framebufferRequest.response != nullptr)
            Debug::InitEarlyTerminal();
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

        //TODO: automate this from ACPI or DTB
        InitTopology();
        
        Filesystem::InitFileCache();
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

        //as a nicety print the known processor topology
        NumaDomain* numaDom = GetTopologyRoot();
        while (numaDom != nullptr)
        {
            numaDom->cpusLock.ReaderLock();
            Log("NUMA domain %lu:", LogLevel::Verbose, numaDom->id);

            CpuDomain* cpuDom = numaDom->cpus;
            while (cpuDom != nullptr)
            {
                Log(" |- CPU domain %lu, online=%s", LogLevel::Verbose,
                    cpuDom->id, cpuDom->online ? "yes" : "no");

                ThreadDomain* threadDom = cpuDom->threads;
                while (threadDom != nullptr)
                {
                    Log("    |- Thread %lu", LogLevel::Verbose, threadDom->id);
                    threadDom = threadDom->next;
                }

                cpuDom = cpuDom->next;
            }

            NumaDomain* next = numaDom->next;
            if (next != nullptr)
                next->cpusLock.ReaderUnlock();
            numaDom->cpusLock.ReaderUnlock();
            numaDom = next;
        }

        Tasking::Thread::Current().Exit(0);
    }

    void InitThread(void*)
    {
        Devices::DeviceManager::Global().Init();
        Drivers::DriverManager::Global().Init();
        Devices::PciBridge::Global().Init();

        for (size_t i = 0; i < 10; i++)
        {
            Log("creating VFS VMO %lu", LogLevel::Debug, i);
            Memory::VmoFileInitArg arg;
            arg.offset = 7;
            arg.filepath = "initdisk/dummy.txt";
            arg.noDeferBacking = false;
            Memory::VmObject vmo(256, (uintptr_t)&arg, VmFlag::File | VmFlag::Write);
            ASSERT(vmo.Valid(), "VMO invalid");

            Log("File contents: %s", LogLevel::Debug, vmo->As<const char>());
        }

        Tasking::Thread::Current().Exit(0);
    }

    bool CoresInEarlyInit()
    {
        return bootloaderRefs.Load(sl::Relaxed) != 0;
    }

    void PerCoreCommonInit()
    {
        Memory::Heap::Global().CreateCaches(); //TODO: does this need to be a member function?
        Debug::InitCoreLogBuffers();
        Interrupts::InitIpiMailbox();

        //TODO: dont forget Tasking::StartSystemClock()
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
