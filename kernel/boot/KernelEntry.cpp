#include <Log.h>
#include <Panic.h>
#include <boot/Limine.h>
#include <arch/ApBoot.h>
#include <memory/PhysicalMemory.h>
#include <memory/Paging.h>
#include <memory/KernelHeap.h>
#include <memory/IpcManager.h>
#include <devices/SystemClock.h>
#include <scheduling/Scheduler.h>
#include <filesystem/Vfs.h>
#include <acpi/AcpiTables.h>
#include <InterruptManager.h>
#include <Configuration.h>

//this is used for demangling stack traces
sl::NativePtr currentProgramElf;
uint64_t Kernel::vmaHighAddr;

extern void QueueInitTasks();

namespace Kernel::Boot
{
    //these are defined in arch/xyz/ArchInit.cpp
    //does arch specific, system-wide init. Returns boot core id.
    extern size_t InitPlatformArch();
    //initializes the local core, using the given id.
    void InitCore(size_t id, size_t acpiId);
    [[noreturn]]
    extern void ExitInitArch();

    //forward decl, used in SetupAllCores()
    [[noreturn]]
    void _ApEntry(Kernel::SmpCoreInfo* smpInfo);
    
    void InitLogging()
    {
        LoggingInitEarly();
#ifdef NORTHPORT_LOG_DEBUGCON_ENABLE_EARLY
        LogEnableDest(LogDest::DebugCon);
        LogEnableColours(LogDest::DebugCon);
#endif
        
        //if the bootloader provided a framebuffer to us, lets use it for logging output for now.
        //once DeviceManager is initialized, it may change this to a more sensible framebuffer.
        if (Kernel::Boot::framebufferRequest.response != nullptr 
            && Kernel::Boot::framebufferRequest.response->framebuffer_count > 0)
        {
            const auto fb = Kernel::Boot::framebufferRequest.response->framebuffers[0];
            LogFramebuffer logFb;
            logFb.base = fb->address;
            logFb.bpp = fb->bpp;
            logFb.width = fb->width;
            logFb.height = fb->height;
            logFb.stride = fb->pitch;
            logFb.pixelMask = 0xFFFF'FFFF;
            logFb.isNotBgr = false;
            if (fb->blue_mask_shift != 0 || fb->red_mask_shift != 24)
                logFb.isNotBgr = true;

            SetLogFramebuffer(logFb);

            //we have a framebuffer, may as well use it for logging.
#ifdef NORTHPORT_LOG_FRAMEBUFFER_ENABLE_EARLY
            LogEnableDest(LogDest::FramebufferOverwrite);
#endif
            LogEnableColours(LogDest::FramebufferOverwrite);
        }


        Log("", LogSeverity::EnumCount); //log empty line so the output of debugcon/serial is starting in a known place.
        Log("Northport kernel succesfully started.", LogSeverity::Info);
    }
    
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

        const sl::BufferView hhdm = PageTableManager::GetHhdm();
        KernelHeap::Global()->Init(hhdm.base.raw + hhdm.length + GB, false);
        Log("Memory init complete.", LogSeverity::Info);
    }

    void InitPlatform()
    {
        Log("Initializing platform ...", LogSeverity::Info);
        Configuration::Global()->Init();
        if (kernelFileRequest.response != nullptr)
        {
            const char* cmdline = kernelFileRequest.response->kernel_file->cmdline;
            Configuration::Global()->SetMany(cmdline);
        }
        else
            Log("Bootloader command line not available.", LogSeverity::Info);
        Configuration::Global()->PrintCurrent(); //this will miss any values from config files, but we can look later as well.
        
        auto panicOnLogError = Configuration::Global()->Get("log_panic_on_error");
        if (panicOnLogError)
            SetPanicOnLogError(panicOnLogError->integer);
        auto enableDebugconLog = Configuration::Global()->Get("log_enable_debugcon");
        if (enableDebugconLog)
            LogEnableDest(LogDest::DebugCon, enableDebugconLog->integer);
        auto enableFramebufferLog = Configuration::Global()->Get("log_enable_framebuffer");
        if (enableFramebufferLog)
            LogEnableDest(LogDest::FramebufferOverwrite, enableFramebufferLog->integer);

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
        Scheduling::Scheduler::Global()->Init();
        Scheduling::Scheduler::Global()->AddProcessor(bootCoreId);

        InitCore(bootCoreId, 0);

        Log("Platform init complete.", LogSeverity::Info);
    }

    void SetupAllCores()
    {
        SmpInfo* smpInfo = BootAPs();
        auto maybeSmpDisabled = Configuration::Global()->Get("boot_disable_smp");
        const bool smpDisabled = maybeSmpDisabled && (maybeSmpDisabled->integer == true);

        for (size_t i = 0; smpInfo->cores[i].apicId != AP_BOOT_APIC_ID_END; i++)
        {
            SmpCoreInfo* coreInfo = &smpInfo->cores[i];

            if (coreInfo->apicId == smpInfo->bspApicId)
            {
                CoreLocal()->acpiProcessorId = coreInfo->acpiProcessorId;
                if (smpDisabled)
                {
                    Log("Kernel only running on boot processor, SMP disabled by configuration entry.", LogSeverity::Info);
                    break;
                }
                else
                    continue;
            }

            if (smpDisabled)
                continue;
            
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
        //nothing to here presently, just a wrapper.
        ExitInitArch();
    }

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
}

extern "C"
{
    void _KernelEntry()
    {
        using namespace Kernel::Boot;
        using namespace Kernel;

        //NOTE: we dont check if the response is valid here. If it's not valid, we're pretty fucked.
        vmaHighAddr = hhdmRequest.response->offset;

        CPU::DisableInterrupts();
        CPU::DoCpuId();
        WriteMsr(MSR_GS_BASE, 0);

        InitLogging();
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
