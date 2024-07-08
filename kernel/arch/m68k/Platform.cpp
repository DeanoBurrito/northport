#include <arch/Platform.h>
#include <arch/m68k/GfPic.h>
#include <debug/Log.h>

namespace Npk
{
    void ExplodeKernelAndReset()
    {
        ASSERT_UNREACHABLE(); //trip reset vector
    }

    uintptr_t MsiAddress(size_t core, size_t vector)
    { 
        (void)core; 
        (void)vector;
        ASSERT_UNREACHABLE(); 
    }

    uintptr_t MsiData(size_t core, size_t vector)
    { 
        (void)core;
        (void)vector;
        ASSERT_UNREACHABLE();
    }

    void MsiExtract(uintptr_t addr, uintptr_t data, size_t& core, size_t& vector)
    { 
        (void)addr;
        (void)data;
        (void)core;
        (void)vector;
        ASSERT_UNREACHABLE();
    }

    void InitTrapFrame(TrapFrame* frame, uintptr_t stack, uintptr_t entry, bool user)
    {
        frame->rte.pc = entry;
        frame->rte.sr = user ? 0 : (1 << 13);
        frame->rte.format = 0; //format 0, no extra info
        frame->a7 = stack;
    }

    void SetTrapFrameArg(TrapFrame* frame, size_t index, void* value)
    {
        if (index >= TrapFrameArgCount)
            return;

        uint32_t* args = &frame->a0;
        args[index] = reinterpret_cast<uint32_t>(value);
    }

    void* GetTrapFrameArg(TrapFrame* frame, size_t index)
    {
        if (index >= TrapFrameArgCount)
            return nullptr;

        uint32_t* args = &frame->a0;
        return reinterpret_cast<void*>(args[index]);
    }

    void InitExtendedRegs(ExtendedRegs** regs)
    {
        (void)regs;
        ASSERT_UNREACHABLE();
    }

    void SaveExtendedRegs(ExtendedRegs* regs)
    {
        (void)regs;
        ASSERT_UNREACHABLE();
    }

    void LoadExtendedRegs(ExtendedRegs* regs)
    {
        (void)regs;
        ASSERT_UNREACHABLE();
    }

    void ExtendedRegsFence()
    {
        ASSERT_UNREACHABLE();
    }

    uintptr_t GetReturnAddr(size_t level, uintptr_t start)
    {
        struct Frame
        {
            Frame* next;
            uintptr_t retAddr;
        };

        Frame* current = reinterpret_cast<Frame*>(start);
        if (start == 0)
            current = static_cast<Frame*>(__builtin_frame_address(0));

        for (size_t i = 0; i <= level; i++)
        {
            if (current == nullptr)
                return 0;
            if (i == level)
                return current->retAddr;
            current = current->next;
        }

        return 0;
    }

    void SendIpi(size_t dest)
    {
        (void)dest;
        ASSERT(false, "Northport does not support m68k SMP.");
    }

    void SetHardwareRunLevel(RunLevel rl)
    {
        uint32_t sr = ReadSr() & 0xF8FF;
        switch (rl)
        {
        case RunLevel::Interrupt:
        case RunLevel::Clock:
            sr |= 7 << 8;
            break;
        default: 
            break;
        }
        WriteSr(sr);
    }

    //NOTE: RoutePinInterrupt() is defined in GfPic.cpp
}
