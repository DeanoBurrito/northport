#include <boot/CommonInit.h>
#include <boot/LimineTags.h>
#include <acpi/Tables.h>
#include <arch/Cpu.h>
#include <arch/Platform.h>
#include <debug/Log.h>
#include <debug/LogBackends.h>
#include <devices/DeviceTree.h>
#include <interrupts/InterruptManager.h>
#include <memory/Pmm.h>
#include <memory/Vmm.h>
#include <tasking/Clock.h>

namespace Npk
{
    uintptr_t hhdmBase;
    uintptr_t hhdmLength;
    
    void InitEarlyPlatform()
    {
        ASSERT(Boot::hhdmRequest.response, "No HHDM response.");
        ASSERT(Boot::memmapRequest.response, "No memory map response.")
        ASSERT(Boot::kernelAddrRequest.response, "No kernel address response.")

        hhdmBase = Boot::hhdmRequest.response->offset;
        auto lastEntry = Boot::memmapRequest.response->entries[Boot::memmapRequest.response->entry_count - 1];
        hhdmLength = sl::AlignUp(lastEntry->base + lastEntry->length, PageSize);
        if (hhdmLength >= HhdmLimit)
        {
            Log("Memory map includes address up to 0x%lx, outside of allowable hhdm range.", LogLevel::Warning, hhdmLength);
            hhdmLength = HhdmLimit;
        }
        Log("Hhdm: base=0x%lx, length=0x%lx", LogLevel::Info, hhdmBase, hhdmLength);

#ifdef NP_X86_64_E9_ALLOWED
        const bool builtInSerial = true;
#else
        const bool builtInSerial = false;
#endif
        if (builtInSerial)
            Debug::EnableLogBackend(Debug::LogBackend::Serial, true);
        
        ScanCpuFeatures();
        LogCpuFeatures();

        if (CpuHasFeature(CpuFeature::VGuest))
            Log("Kernel is running as virtualized guest.", LogLevel::Info);
        if (Boot::bootloaderInfoRequest.response != nullptr)
            Log("Loaded by: %s v%s", LogLevel::Info, Boot::bootloaderInfoRequest.response->name, Boot::bootloaderInfoRequest.response->version);
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
            Acpi::SetRsdp(Boot::rsdpRequest.response->address);
        else
            Log("Bootloader did not provide RSDP (or it was null).", LogLevel::Warning);
        
        if (Boot::dtbRequest.response != nullptr && Boot::dtbRequest.response->dtb_ptr != nullptr)
            Devices::DeviceTree::Global().Init((uintptr_t)Boot::dtbRequest.response->dtb_ptr);
        else
            Log("Bootloader did not provide DTB (or it was null).", LogLevel::Warning);
    }

    [[noreturn]]
    void ExitBspInit()
    {
        //TODO: reclaim bootloader memory here?
        Tasking::StartSystemClock();
        Halt();
    }

    [[noreturn]]
    void ExitApInit()
    {
        Halt();
    }
}
