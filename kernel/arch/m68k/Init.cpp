#include <arch/Platform.h>
#include <arch/Timers.h>
#include <boot/CommonInit.h>
#include <boot/LimineTags.h>
#include <memory/Vmm.h>
#include <debug/Log.h>
#include <tasking/Clock.h>
#include <NativePtr.h>

namespace Npk
{
    void InitCore(size_t coreId)
    {}

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
        InitTimers();

        if (Boot::smpRequest.response != nullptr)
        { ASSERT_UNREACHABLE(); } //sounds exciting, I didnt know these existed
        else
            InitCore(0);

        Tasking::StartSystemClock();
        ExitCoreInit();
        ASSERT_UNREACHABLE();
    }
}
