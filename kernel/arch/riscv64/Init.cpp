#include <arch/riscv64/Interrupts.h>
#include <arch/riscv64/IntControllers.h>
#include <arch/Timers.h>
#include <arch/Platform.h>
#include <arch/Cpu.h>
#include <boot/CommonInit.h>
#include <boot/LimineTags.h>
#include <config/DeviceTree.h>
#include <debug/Log.h>
#include <memory/Vmm.h>

namespace Npk
{
    void InitCore(size_t id)
    {
        VMM::Kernel().MakeActive();
        
        BlockSumac();
        LoadStvec();

        ClearCsrBits("sstatus", 1 << 19); //disable MXR.

        CoreLocalInfo* clb = new CoreLocalInfo();
        clb->id = id;
        clb->runLevel = RunLevel::Normal;
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
        
        InitCore(info->hartid);
        ExitApInit();
    }

    sl::NativePtr uartRegs;
    void UartWrite(const char* str, size_t length)
    {
        for (size_t i = 0; i < length; i++)
        {
            while ((uartRegs.Offset(5).Read<uint8_t>() & (1 << 5)) == 0)
                sl::HintSpinloop();
            uartRegs.Write<uint8_t>(str[i]);
        }
    }

    void TryInitUart()
    {
        Config::DtNode* uart = Config::DeviceTree::Global().FindCompatible("ns16550a");
        if (uart == nullptr)
            return;

        Config::DtProp* regsProp = uart->FindProp("reg");
        if (regsProp == nullptr)
            return;
        
        Config::DtPair reg;
        regsProp->ReadRegs({ &reg, 1 });
        uartRegs = Npk::hhdmBase + reg[0];

        Debug::AddEarlyLogOutput(UartWrite);
    }
}

extern "C"
{
    struct EntryData
    {
        uintptr_t hartId;
        uintptr_t dtb;
        uintptr_t physBase;
        uintptr_t virtBase;
    };

    void KernelEntry()
    {
        using namespace Npk;
        
        //these ensure we don't try to load bogus values as the core-local block.
        asm volatile("mv tp, zero"); //implicitly volatile, but for clarity
        asm volatile("csrw sscratch, zero"); 

        InitEarlyPlatform();
        InitMemory();
        InitPlatform();

        TryInitUart();
        InitIntControllers(); //determine system-level controllers and bring them up.
        InitTimers();

        if (Boot::smpRequest.response != nullptr)
        {
            auto resp = Boot::smpRequest.response;
            for (size_t i = 0; i < resp->cpu_count; i++)
            {
                auto cpuInfo = resp->cpus[i];
                if (cpuInfo->hartid == resp->bsp_hartid)
                {
                    InitCore(cpuInfo->hartid);
                    continue;
                }

                cpuInfo->goto_address = ApEntry;
                Log("Sending bring-up request to core %lu.", LogLevel::Verbose, cpuInfo->hartid);
            }
        }

        ExitBspInit();
    }
}
