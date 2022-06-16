#include <Log.h>
#include <Panic.h>
#include <boot/Limine.h>
#include <memory/PhysicalMemory.h>
#include <memory/Paging.h>
#include <memory/KernelHeap.h>
#include <memory/IpcManager.h>
#include <devices/SystemClock.h>
#include <scheduling/Scheduler.h>
#include <filesystem/Vfs.h>
#include <acpi/AcpiTables.h>
#include <InterruptManager.h>

//TODO: platform specific stuff, lets get rid of it
#include <arch/x86_64/ApBoot.h>
#include <devices/LApic.h>

//this is used for demangling stack traces
sl::NativePtr currentProgramElf;
uint64_t Kernel::vmaHighAddr;

//forward decl, used in SetupAllCores()
[[noreturn]]
extern "C" void _ApEntry(Kernel::SmpCoreInfo* smpInfo);
extern void QueueInitTasks();

namespace Kernel::Boot
{
    //these are defined in arch/xyz/ArchInit.cpp
    //does arch specific, system-wide init. Returns boot core id.
    extern size_t InitPlatformArch();
    //initializes the local core, using the given id.
    void InitCore(size_t id, size_t acpiId);
    
    void InitMemory()
    {
        using namespace Memory;
        Log("Initializing memory ...", LogSeverity::Info);

        if (memmapRequest.response == nullptr)
            Log("Bootloader did not provide memory map, cannot continue kernel init.", LogSeverity::Fatal);
        if (kernelAddrRequest.response == nullptr)
            Log("Bootloader did not provide kernel virtual to physical map, cannot continue kernel init.", LogSeverity::Fatal); 

        PMM::Global()->InitFromLimine();
        PageTableManager::Setup();
        PageTableManager::Current()->InitKernelFromLimine();
        PageTableManager::Current()->MakeActive();

        const sl::BufferView hhdm = PageTableManager::GetHhdm();
        KernelHeap::Global()->Init(hhdm.base.raw + hhdm.length + 2 * GB, false);
        Log("Memory init complete.", LogSeverity::Info);
    }

    void InitPlatform()
    {
        Log("Initializing platform ...", LogSeverity::Info);

        //try get kernel elf file, for debugging symbols
        if (kernelFileRequest.response == nullptr)
        {
            currentProgramElf.ptr = nullptr;
            Log("Unable to get kernel elf from bootloader, debug symbols unavailable.", LogSeverity::Warning);
        }
        else
        {
            currentProgramElf.ptr = kernelFileRequest.response->kernel_file->address;
            Logf("Kernel elf located at 0x%lx", LogSeverity::Info, currentProgramElf.raw);
        }

        if (rsdpRequest.response == nullptr)
            Log("RSDP not provided by bootloader, cannot continue kernel init.", LogSeverity::Fatal);
        ACPI::AcpiTables::Global()->Init((uint64_t)rsdpRequest.response->address);

        const size_t bootCoreId = InitPlatformArch();

        InitPanic();
        InterruptManager::Global()->Init();

        Filesystem::VFS::Global()->Init();
        Memory::IpcManager::Global()->Init();
        Scheduling::Scheduler::Global()->Init(bootCoreId);

        InitCore(bootCoreId, 0);

        Log("Platform init complete.", LogSeverity::Info);
    }

    void SetupAllCores()
    {
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
    [[noreturn]]
    void _ApEntry(Kernel::SmpCoreInfo* smpInfo)
    {
        using namespace Kernel;
        Memory::PageTableManager::Current()->MakeActive();
        smpInfo = EnsureHigherHalfAddr(smpInfo);

        Boot::InitCore(smpInfo->apicId, smpInfo->acpiProcessorId);

        Boot::ExitInit();
        __builtin_unreachable();
    }
    
    void _KernelEntry()
    {
        using namespace Kernel::Boot;
        using namespace Kernel;

        //NOTE: we dont check if the response is valid here. If it's not valid, we're pretty fucked.
        vmaHighAddr = hhdmRequest.response->offset;

        CPU::ClearInterruptsFlag();
        CPU::DoCpuId();
        CPU::WriteMsr(MSR_GS_BASE, 0);

        LoggingInitEarly();
#ifdef NORTHPORT_ENABLE_DEBUGCON_LOG_AT_BOOT
        EnableLogDestinaton(LogDestination::DebugCon);
#endif
        Log("", LogSeverity::EnumCount); //log empty line so the output of debugcon/serial is starting in a known place.
        Log("Northport kernel succesfully started.", LogSeverity::Info);
        
        InitMemory();
        LoggingInitFull();
        PrintBootInfo();
        //TODO: would be nice to have PrintCpuInfo() as well

        InitPlatform();
        SetupAllCores();

        QueueInitTasks();
        ExitInit();
        __builtin_unreachable();
    }
}
