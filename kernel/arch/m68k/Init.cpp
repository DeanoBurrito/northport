#include <arch/Platform.h>
#include <arch/Timers.h>
#include <arch/m68k/Interrupts.h>
#include <boot/CommonInit.h>
#include <boot/LimineTags.h>
#include <memory/Vmm.h>
#include <debug/Log.h>
#include <tasking/Clock.h>
#include <NativePtr.h>

namespace Npk
{
    CoreLocalInfo* coreLocalControl;

    void InitCore(size_t coreId, uint32_t acpiId)
    {
        CoreLocalInfo* clb = new CoreLocalInfo();
        clb->id = coreId;
        clb->acpiId = acpiId;
        clb->runLevel = RunLevel::Dpc;
        clb->nextStack = nullptr;
        coreLocalControl = clb;

        LoadVectorTable();
        PerCoreCommonInit();
    }

    void ThreadedArchInit()
    {} //no-op
}

extern "C"
{
#define NP_M68K_ASSUME_TTY 0xff008000
#ifdef NP_M68K_ASSUME_TTY
    sl::NativePtr ttyRegs;

    static void TtyWrite(sl::StringSpan text)
    {
        for (size_t i = 0; i < text.Size(); i++)
            ttyRegs.Write<uint32_t>(text[i]); //device regs are 32-bits wide
    }

    Npk::Debug::LogOutput ttyOutput
    {
        .Write = TtyWrite,
        .BeginPanic = nullptr
    };
#endif

    void KernelEntry()
    {
        using namespace Npk;

        coreLocalControl = nullptr;
        InitEarlyPlatform();
        InitMemory();

#ifdef NP_M68K_ASSUME_TTY
        auto maybeTtyRegs = VMM::Kernel().Alloc(0x20, NP_M68K_ASSUME_TTY, VmFlag::Write | VmFlag::Mmio);
        if (maybeTtyRegs.HasValue())
        {
            ttyRegs = *maybeTtyRegs;
            ttyRegs.Offset(8).Write<uint32_t>(0); //disable interrupts from the device
            Debug::AddLogOutput(&ttyOutput);
        }
#endif
        InitPlatform();

        if (Boot::smpRequest.response != nullptr)
        {  
            for (size_t i = 0; i < Boot::smpRequest.response->cpu_count; i++)
            {
                limine_smp_info* procInfo = Boot::smpRequest.response->cpus[i];
                if (procInfo->id == Boot::smpRequest.response->bsp_id)
                {
                    InitCore(procInfo->id, procInfo->processor_id);
                    continue;
                }

                ASSERT_UNREACHABLE(); //TODO: we dont support multi-core (yet)
            }
        }
        else
            InitCore(0, 0);

        InitTimers();
        Tasking::StartSystemClock();
        ExitCoreInit();
        ASSERT_UNREACHABLE();
    }
}
