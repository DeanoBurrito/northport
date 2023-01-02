#include <arch/riscv64/Interrupts.h>
#include <arch/Timers.h>
#include <arch/Platform.h>
#include <boot/CommonInit.h>
#include <boot/LimineTags.h>
#include <boot/LimineBootstrap.h>
#include <config/DeviceTree.h>
#include <debug/Log.h>
#include <memory/Vmm.h>

namespace Npk
{
    void InitCore(size_t id, uint32_t plicContext)
    {
        VMM::Kernel().MakeActive();
        
        BlockSumac();
        LoadStvec();

        ClearCsrBits("sstatus", 1 << 19); //disable MXR.

        CoreLocalInfo* clb = new CoreLocalInfo();
        clb->id = id;
        clb->selfAddr = (uintptr_t)clb;
        clb->interruptControl = plicContext;
        clb->runLevel = RunLevel::Normal;
        // clb->nextKernelStack = (new uint8_t[0x800]) + 0x800; //TODO: not this
        WriteCsr("sscratch", (uintptr_t)clb);

        EnableInterrupts();
        Log("Core %lu finished core init.", LogLevel::Info, id);
    }

    void ApEntry(limine_smp_info* info)
    {
        InitCore(info->hart_id, info->plic_context);
        ExitApInit();
    }

    sl::NativePtr uartRegs;
    void UartWrite(const char* str, size_t length)
    {
        for (size_t i = 0; i < length; i++)
        {
            while ((uartRegs.Offset(5).Read<uint8_t>() & (1 << 5)) == 0);
            uartRegs.Write<uint8_t>(str[i]);
        }
    }

    void TryInitUart()
    {
        auto maybeUart = Config::DeviceTree::Global().GetCompatibleNode("ns16550a");
        if (!maybeUart)
            return;

        auto regsProp = maybeUart->GetProp("reg");
        if (!regsProp)
            return;
        
        uintptr_t base;
        regsProp->ReadRegs(*maybeUart, &base, nullptr);
        uartRegs = base + Npk::hhdmBase;

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

    void KernelEntry(EntryData* data)
    {
        using namespace Npk;
        
        WriteCsr("sscratch", 0);
        Boot::PerformLimineBootstrap(data->physBase, data->virtBase, data->hartId, data->dtb);
        data = nullptr;
        (void)data;

        InitEarlyPlatform();
        InitMemory();
        InitPlatform();

        TryInitUart();
        InitTimers();

        if (Boot::smpRequest.response != nullptr)
        {
            for (size_t i = 0; i < Boot::smpRequest.response->cpu_count; i++)
            {
                limine_smp_info* procInfo = Boot::smpRequest.response->cpus[i];
                if (procInfo->hart_id == Boot::smpRequest.response->bsp_hart_id)
                {
                    InitCore(procInfo->hart_id, procInfo->plic_context);
                    continue;
                }
                
                procInfo->goto_address = ApEntry;
                Log("Sending bring-up request to core %lu.", LogLevel::Verbose, procInfo->hart_id);
            }
        }

        ExitBspInit();
    }
}
