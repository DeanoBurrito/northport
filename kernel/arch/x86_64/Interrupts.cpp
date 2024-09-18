#include <arch/Interrupts.h>
#include <arch/Misc.h>
#include <Memory.h>
#include <Maths.h>

#include <core/Log.h>

namespace Npk
{
    bool SendIpi(size_t dest)
    { ASSERT_UNREACHABLE(); }

    void SetHardwareRunLevel(RunLevel rl)
    {
        switch (rl)
        {
        case RunLevel::Interrupt:
        case RunLevel::Clock:
            return DisableInterrupts();
        default:
            return EnableInterrupts();
        }
    }

    bool RoutePinInterrupt(size_t pin, size_t core, size_t vector)
    { ASSERT_UNREACHABLE(); }

    sl::Opt<MsiConfig> ConstructMsi(size_t core, size_t vector)
    {
        MsiConfig cfg;
        cfg.address = ((core & 0xFF) << 12) | 0xFEE0'0000;
        cfg.data = vector & 0xFF;

        return cfg;
    }

    bool DeconstructMsi(MsiConfig cfg, size_t& core, size_t& vector)
    {
        core = (cfg.address >> 12) & 0xFF;
        vector = cfg.data & 0xFF;
        return true;
    }

    struct TrapFrame
    {
        uint64_t r15;
        uint64_t r14;
        uint64_t r13;
        uint64_t r12;
        uint64_t r11;
        uint64_t r10;
        uint64_t r9;
        uint64_t r8;
        uint64_t rbp;
        uint64_t rdi;
        uint64_t rsi;
        uint64_t rdx;
        uint64_t rcx;
        uint64_t rbx;
        uint64_t rax;

        uint64_t vector;
        uint64_t ec;

        struct
        {
            uint64_t rip;
            uint64_t cs;
            uint64_t flags;
            uint64_t rsp;
            uint64_t ss;
        } iret;
    };

    static_assert(sizeof(TrapFrame) == 176, "x86_64 TrapFrame size changed, update assembly sources.");

    void InitTrapFrame(TrapFrame* frame, uintptr_t stack, uintptr_t entry, bool user)
    {
        sl::memset(frame, 0, sizeof(TrapFrame));

        frame->iret.cs = user ? SelectorUserCode : SelectorKernelCode;
        frame->iret.ss = user ? SelectorUserData : SelectorKernelData;
        frame->iret.rsp = sl::AlignDown(stack, 16);
        frame->iret.rip = entry;
        frame->iret.flags = 0x202;
        frame->rbp = 0;
    }

    void SetTrapFrameArg(TrapFrame* frame, size_t index, void* value)
    {
        const uint64_t val = reinterpret_cast<uint64_t>(value);
        switch (index)
        {
        case 0: 
            frame->rdi = val; 
            return;
        case 1: 
            frame->rsi = val; 
            return;
        case 2: 
            frame->rdx = val; 
            return;
        case 3: 
            frame->rcx = val; 
            return;
        case 4: 
            frame->r8 = val; 
            return;
        case 5: 
            frame->r9 = val; 
            return;
        }
    }

    void* GetTrapFrameArg(TrapFrame* frame, size_t index)
    {
        uint64_t value = 0;
        switch (index)
        {
        case 0:
            value = frame->rdi;
            break;
        case 1:
            value = frame->rsi;
            break;
        case 2:
            value = frame->rdx;
            break;
        case 3:
            value = frame->rcx;
            break;
        case 4:
            value = frame->r8;
            break;
        case 5:
            value = frame->r9;
            break;
        }

        return reinterpret_cast<void*>(value);
    }

    extern "C" void TrapDispatch(TrapFrame* frame)
    {}
}
