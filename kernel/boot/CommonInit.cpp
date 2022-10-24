#include <boot/CommonInit.h>
#include <boot/LimineTags.h>
#include <arch/Cpu.h>
#include <arch/Platform.h>
#include <arch/Paging.h>
#include <config/DeviceTree.h>
#include <config/AcpiTables.h>
#include <debug/Log.h>
#include <debug/LogBackends.h>
#include <interrupts/InterruptManager.h>
#include <interrupts/Ipi.h>
#include <memory/Pmm.h>
#include <memory/Vmm.h>
#include <tasking/Clock.h>

namespace Npk
{
    uintptr_t hhdmBase;
    uintptr_t hhdmLength;
    size_t bootDataReferences;
    
    void InitEarlyPlatform()
    {
        ASSERT(Boot::hhdmRequest.response, "No HHDM response.");
        ASSERT(Boot::memmapRequest.response, "No memory map response.")
        ASSERT(Boot::kernelAddrRequest.response, "No kernel address response.")

        hhdmBase = Boot::hhdmRequest.response->offset;
        auto lastEntry = Boot::memmapRequest.response->entries[Boot::memmapRequest.response->entry_count - 1];
        hhdmLength = sl::AlignUp(lastEntry->base + lastEntry->length, GiB); //Hhdm is 1GiB aligned.
        if (hhdmLength >= GetHhdmLimit())
        {
            Log("Memory map includes address up to 0x%lx, outside of allowable hhdm range.", LogLevel::Warning, hhdmLength);
            hhdmLength = GetHhdmLimit();
        }
        Log("Hhdm: base=0x%lx, length=0x%lx, limit=0x%lx", LogLevel::Info, hhdmBase, hhdmLength, GetHhdmLimit());

#ifdef NP_X86_64_E9_ALLOWED
        Debug::EnableLogBackend(Debug::LogBackend::Debugcon, true);
#elif defined(__x86_64__)
        Debug::EnableLogBackend(Debug::LogBackend::SerialNs16550, true);
#endif
        if (Config::DeviceTree::Global().GetCompatibleNode("ns16550a") 
            || Config::DeviceTree::Global().GetCompatibleNode("ns16550"))
            Debug::EnableLogBackend(Debug::LogBackend::SerialNs16550, true);
        
        ScanCpuFeatures();
        LogCpuFeatures();

        if (CpuHasFeature(CpuFeature::VGuest))
            Log("Kernel is running as virtualized guest.", LogLevel::Info);
        if (Boot::bootloaderInfoRequest.response != nullptr)
            Log("Loaded by: %s v%s", LogLevel::Info, Boot::bootloaderInfoRequest.response->name, Boot::bootloaderInfoRequest.response->version);

        if (Boot::smpRequest.response == nullptr)
            bootDataReferences = 1;
        else
            bootDataReferences = Boot::smpRequest.response->cpu_count;
    }

    void InitMemory()
    {
        PMM::Global().Init();
        VMM::InitKernel();
    }

    void InitPlatform()
    {
        Interrupts::InterruptManager::Global().Init();

        if (Boot::framebufferRequest.response != nullptr)
            Debug::EnableLogBackend(Debug::LogBackend::Terminal, true);
        else
            Log("Bootloader did not provide framebuffer.", LogLevel::Warning);
        
        if (Boot::rsdpRequest.response != nullptr && Boot::rsdpRequest.response != nullptr)
            Config::SetRsdp(Boot::rsdpRequest.response->address);
        else
            Log("Bootloader did not provide RSDP (or it was null).", LogLevel::Warning);
        
        if (Boot::dtbRequest.response != nullptr && Boot::dtbRequest.response->dtb_ptr != nullptr)
            Config::DeviceTree::Global().Init((uintptr_t)Boot::dtbRequest.response->dtb_ptr);
        else
            Log("Bootloader did not provide DTB (or it was null).", LogLevel::Warning);
        
        if (Config::DeviceTree::Global().GetCompatibleNode("ns16550a")
            || Config::DeviceTree::Global().GetCompatibleNode("ns16550"))
            Debug::EnableLogBackend(Debug::LogBackend::SerialNs16550, true);
    }

    void ReclaimBootloaderMemory()
    {
        //since the memory map is contained within the memory we're going to reclaim, we'll need our own copy.
        size_t reclaimCount = 0;
        limine_memmap_entry reclaimEntries[Boot::memmapRequest.response->entry_count];
        for (size_t i = 0; i < Boot::memmapRequest.response->entry_count; i++)
        {
            if (Boot::memmapRequest.response->entries[i]->type != LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE)
                continue;
            reclaimEntries[reclaimCount++] = *Boot::memmapRequest.response->entries[i];
        }

        for (size_t i = 0; i < reclaimCount; i++)
            PMM::Global().IngestMemory(reclaimEntries[i].base, reclaimEntries[i].length);
        //Feel free to enable this for cool diagnostics, beware the boot-time increase.
        // PMM::Global().DumpState();

        Log("Bootloader memory no-longer in use, reclaimed %lu entries.", LogLevel::Info, reclaimCount);
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
        const size_t refsLeft = __atomic_sub_fetch(&bootDataReferences, 1, __ATOMIC_RELAXED);
        if (refsLeft == 0)
            ReclaimBootloaderMemory();
        
        Interrupts::InitIpiMailbox();
        Halt();
    }
}
