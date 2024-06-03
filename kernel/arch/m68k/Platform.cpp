#include <arch/Platform.h>
#include <debug/Log.h>

namespace Npk
{
    void ExplodeKernelAndReset()
    {
        ASSERT_UNREACHABLE(); //trip reset vector
    }

    //afaik m68k systems are all single-core, so we use a global variable here
    CoreLocalInfo coreLocalInfo;
    CoreLocalInfo& CoreLocal()
    { return coreLocalInfo; }

    uintptr_t MsiAddress(size_t core, size_t vector)
    { ASSERT_UNREACHABLE(); }

    uintptr_t MsiData(size_t core, size_t vector)
    { ASSERT_UNREACHABLE(); }

    void MsiExtract(uintptr_t addr, uintptr_t data, size_t& core, size_t& vector)
    { ASSERT_UNREACHABLE(); }

    void InitTrapFrame(TrapFrame* frame, uintptr_t stack, uintptr_t entry, bool user)
    {
        ASSERT_UNREACHABLE();
    }

    void SetTrapFrameArg(TrapFrame* frame, size_t index, void* value)
    {
        ASSERT_UNREACHABLE();
    }

    void* GetTrapFrameArg(TrapFrame* frame, size_t index)
    {
        ASSERT_UNREACHABLE();
    }

    void InitExtendedRegs(ExtendedRegs** regs)
    {
        ASSERT_UNREACHABLE();
    }

    void SaveExtendedRegs(ExtendedRegs* regs)
    {
        ASSERT_UNREACHABLE();
    }

    void LoadExtendedRegs(ExtendedRegs* regs)
    {
        ASSERT_UNREACHABLE();
    }

    void ExtendedRegsFence()
    {
        ASSERT_UNREACHABLE();
    }

    uintptr_t GetReturnAddr(size_t level, uintptr_t start)
    {
        ASSERT_UNREACHABLE();
    }

    void SendIpi(size_t dest)
    {
        ASSERT_UNREACHABLE();
    }

    void SetHardwareRunLevel(RunLevel rl)
    {
        ASSERT_UNREACHABLE();
    }

    bool RoutePinInterrupt(size_t core, size_t vector, size_t gsi)
    {
        ASSERT_UNREACHABLE();
    }
}
