#include <arch/Init.h>
#include <arch/Platform.h>
#include <arch/m68k/GfPic.h>
#include <arch/m68k/Interrupts.h>
#include <debug/Log.h>
#include <memory/Vmm.h>
#include <NativePtr.h>

namespace Npk
{
#define NPK_M68K_ASSUME_TTY 0xff008000
#ifdef NPK_M68K_ASSUME_TTY
    sl::NativePtr ttyRegs;

    static void TtyWrite(sl::StringSpan text)
    {
        for (size_t i = 0; i < text.Size(); i++)
            ttyRegs.Write<uint32_t>(text[i]); //device regs are 32-bits wide
    }

    Debug::LogOutput ttyOutput
    {
        .Write = TtyWrite,
        .BeginPanic = nullptr
    };
#endif
    CoreLocalInfo* coreLocalBlocks = nullptr;

    void ArchKernelEntry()
    {} //no-op

    void ArchLateKernelEntry()
    {
#ifdef NPK_M68K_ASSUME_TTY
        auto maybeTtyRegs = VMM::Kernel().Alloc(0x20, NPK_M68K_ASSUME_TTY, VmFlag::Mmio | VmFlag::Write);
        if (maybeTtyRegs)
        {
            ttyRegs = *maybeTtyRegs;
            ttyRegs.Offset(8).Write<uint32_t>(0); //disable interrupts from uart
            Debug::AddLogOutput(&ttyOutput);
        }
#endif

        InitPics();
    }

    void ArchInitCore(size_t myId)
    {
        LoadVectorTable();

        CoreLocalInfo* clb = new CoreLocalInfo();
        coreLocalBlocks = clb;
        clb->id = myId;
        clb->runLevel = RunLevel::Dpc;
    }

    void ArchThreadedInit()
    {} //no-op
}
