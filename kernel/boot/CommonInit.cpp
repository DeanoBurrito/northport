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
        ASSERT(Boot::hhdmRequest.response, "No HHDM response.");
        ASSERT(Boot::memmapRequest.response, "No memory map response.")
        ASSERT(Boot::kernelAddrRequest.response, "No kernel address response.")

        hhdmBase = Boot::hhdmRequest.response->offset;
        auto lastEntry = Boot::memmapRequest.response->entries[Boot::memmapRequest.response->entry_count - 1];
        hhdmLength = sl::AlignUp(lastEntry->base + lastEntry->length, PageSize);
        Log("Hhdm: base=0x%lx, length=0x%lx", LogLevel::Info, hhdmBase, hhdmLength);

        if (CpuHasFeature(CpuFeature::VGuest))
            Log("Kernel is running as virtualized guest.", LogLevel::Info);
        
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
            Config::DeviceTree::Global().Init((uintptr_t)dtbRequest.response->dtb_ptr);
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

    void ProbeDtbDrivers()
    {
        //we make this assumption below when generating driver names.
        ASSERT(sizeof(char) == sizeof(uint8_t), "Oof");

        using namespace Config;
        sl::Vector<DtNode> compatibles;
        sl::Vector<DtNode> parents;
        size_t totalNodes = 1;
        parents.PushBack(*DeviceTree::Global().GetNode("/"));

        while (!parents.Empty())
        {
            const DtNode node = parents.PopBack();
            totalNodes += node.childCount;

            for (size_t i = 0; i < node.childCount; i++)
            {
                auto maybeChild = DeviceTree::Global().GetChild(node, i);
                if (!maybeChild)
                    continue;
                
                if (maybeChild->childCount > 0)
                    parents.PushBack(*maybeChild);

                auto maybeCompat = maybeChild->GetProp("compatible");
                if (maybeCompat)
                    compatibles.PushBack(*maybeChild);
            }
        }

        Log("Searched %lu dtb nodes, found %lu with 'compatible' properties.", LogLevel::Info,
            totalNodes, compatibles.Size());
        
        for (size_t i = 0; i < compatibles.Size(); i++)
        {
            const DtProperty prop = compatibles[i].GetProp("compatible").Value();
            const char* nameTemplate = "dtbc%s";
            const char* propStr = prop.ReadStr(0);

            using namespace Drivers;
            DeviceTreeInitTag* initTag = new DeviceTreeInitTag(compatibles[i], nullptr);

            //NOTE: that this property lists multiple strings (which we handle), starting with
            //the most specific device name. This means we'll load specific drivers before more 
            //generic ones, which is a nice behaviour to have.
            bool success = false;
            for (size_t i = 0; (propStr = prop.ReadStr(i)) != nullptr; i++)
            {
                const size_t nameLen = npf_snprintf(nullptr, 0, nameTemplate, propStr) + 1;
                char driverName[nameLen];
                npf_snprintf(driverName, nameLen, nameTemplate, propStr);

                //see the 'oof' assert early in this function.
                ManifestName manifestName(reinterpret_cast<uint8_t*>(driverName), nameLen);
                success = DriverManager::Global().TryLoadDriver(manifestName, initTag);
                if (success)
                    break;
            }

            if (!success)
                delete initTag;
        }
    }

    void InitThread(void*)
    {
        Devices::DeviceManager::Global().Init();
        Drivers::DriverManager::Global().Init();
        Devices::PciBridge::Global().Init();

        //manually search the device tree for any devices to load drivers for.
        if (Config::DeviceTree::Global().Available())
            ProbeDtbDrivers();

        Tasking::Thread::Current().Exit(0);
    }

    [[noreturn]]
    void ExitBspInit()
    {
        Tasking::StartSystemClock();
        ExitApInit();
    }

    [[noreturn]]
    void ExitApInit()
    {
        Debug::InitCoreLogBuffers();
        Interrupts::InitIpiMailbox();

        using namespace Tasking;
        if (--bootloaderRefs > 0)
            Scheduler::Global().RegisterCore();
        else
        {
            auto reclaimThread = Scheduler::Global().CreateThread(ReclaimMemoryThread, nullptr);
            Scheduler::Global().RegisterCore(reclaimThread);
        }
        ASSERT_UNREACHABLE();
    }
}
