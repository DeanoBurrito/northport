#include <arch/riscv64/Interrupts.h>
#include <arch/riscv64/IntrControllers.h>
#include <arch/Timers.h>
#include <arch/Platform.h>
#include <arch/Cpu.h>
#include <boot/CommonInit.h>
#include <boot/LimineTags.h>
#include <debug/Log.h>
#include <memory/Vmm.h>

#include <tasking/Clock.h>

namespace Npk
{
    void InitCore(uintptr_t id, uintptr_t acpiId)
    {
        VMM::Kernel().MakeActive();
        
        BlockSumac();
        LoadStvec();

        ClearCsrBits("sstatus", 1 << 19); //disable MXR.

        CoreLocalInfo* clb = new CoreLocalInfo();
        clb->id = id;
        clb->acpiId = acpiId;
        clb->runLevel = RunLevel::Dpc;
        asm volatile("mv tp, %0" :: "r"((uint64_t)clb));

        CoreConfig* config = new CoreConfig();
        CoreLocal()[LocalPtr::Config] = config;
        ScanLocalCpuFeatures();
        ScanLocalTopology();

        if (CpuHasFeature(CpuFeature::QuadFPU))
            config->extRegsBufferSize = 16 * 32 + 4; //32 regs, 16 bytes each, +4 for FCSR
        else if (CpuHasFeature(CpuFeature::DoubleFPU))
            config->extRegsBufferSize = 8 * 32 + 4;
        else if (CpuHasFeature(CpuFeature::SingleFPU))
            config->extRegsBufferSize = 4 * 32 + 4;
        else
            config->extRegsBufferSize = 0;

        EnableInterrupts();
        Log("Core %lu finished core init.", LogLevel::Info, id);
    }

    void ApEntry(limine_smp_info* info)
    {
        asm volatile("mv tp, zero"); 
        asm volatile("csrw sscratch, zero");
        InitCore(info->hartid, info->processor_id);

        PerCoreCommonInit();
        ExitCoreInit();
        ASSERT_UNREACHABLE();
    }

#ifdef NP_RISCV64_ASSUME_SERIAL
    sl::NativePtr uartRegs;
    void UartWrite(sl::StringSpan text)
    {
        for (size_t i = 0; i < text.Size(); i++)
        {
            while ((uartRegs.Offset(5).Read<uint8_t>() & (1 << 5)) == 0)
                sl::HintSpinloop();
            uartRegs.Write<uint8_t>(text[i]);
        }
    }

    Debug::LogOutput uartLogOutput
    {
        .Write = UartWrite,
        .BeginPanic = nullptr
    };
#endif

    void ThreadedArchInit()
    {}
}

extern "C"
{
    void KernelEntry()
    {
        using namespace Npk;
        
        //these ensure we don't try to load bogus values as the core-local block.
        asm volatile("mv tp, zero"); //implicitly volatile, but for clarity
        asm volatile("csrw sscratch, zero"); 

        InitEarlyPlatform();

#ifdef NP_RISCV64_ASSUME_SERIAL
        uartRegs = Npk::hhdmBase + NP_RISCV64_ASSUME_SERIAL;
        Debug::AddLogOutput(&uartLogOutput);
#endif

        InitMemory();
        InitPlatform();
        //InitIntControllers(); 
        InitTimers();

        if (Boot::smpRequest.response != nullptr)
        {
            auto resp = Boot::smpRequest.response;
            for (size_t i = 0; i < resp->cpu_count; i++)
            {
                auto cpuInfo = resp->cpus[i];
                if (cpuInfo->hartid == resp->bsp_hartid)
                {
                    InitCore(cpuInfo->hartid, cpuInfo->processor_id);
                    PerCoreCommonInit();
                    continue;
                }

                cpuInfo->goto_address = ApEntry;
                Log("Sending bring-up request to core %lu.", LogLevel::Verbose, cpuInfo->hartid);
            }
        }
        else
        {
            InitCore(0, 0);
            PerCoreCommonInit();
        }

        Tasking::StartSystemClock();
        ExitCoreInit();
        ASSERT_UNREACHABLE();
    }
}
