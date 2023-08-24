#include <arch/riscv64/Interrupts.h>
#include <debug/Log.h>
#include <interrupts/InterruptManager.h>
#include <interrupts/Ipi.h>
#include <memory/Vmm.h>
#include <tasking/Scheduler.h>

namespace Npk
{
    extern uint8_t TrapEntry[] asm("TrapEntry");
    
    void LoadStvec()
    {
        ASSERT(((uintptr_t)TrapEntry & 3) == 0, "Trap entry address misaligned.");

        //we use direct interrupts so we leave the mode bits clear.
        constexpr uintptr_t ModeMask = ~(uintptr_t)0b11;
        WriteCsr("stvec", (uintptr_t)TrapEntry & ModeMask);

        //enable supervisor interrupts: software, timer, external.
        SetCsrBits("sie", 0x222);
    }

    extern void (*timerCallback)();
}

extern "C"
{
    constexpr const char* exceptionNames[] = 
    {
        "instruction address misaligned",
        "instruction access fault",
        "illegal instruction",
        "breakpoint",
        "load address misaligned",
        "load access fault",
        "store/AMO address misaligned",
        "store/AMO access fault",
        "ecall from U-mode",
        "ecall from S-mode"
    };
    
    void TrapDispatch(Npk::TrapFrame* frame)
    {
        using namespace Npk;
        const RunLevel prevRunLevel = CoreLocal().runLevel;
        CoreLocal().runLevel = RunLevel::IntHandler;
        Tasking::Scheduler::Global().SavePrevFrame(frame, prevRunLevel);

        const bool isInterrupt = frame->vector & (1ul << 63);
        frame->vector &= ~(1ul << 63);

        if (isInterrupt)
        {
            switch (frame->vector)
            {
            case 1:
                Interrupts::ProcessIpiMail();
                break;
            case 5:
                if (timerCallback != nullptr)
                    timerCallback();
                break;
            case 9:
                Log("Got riscv external interrupt.", LogLevel::Fatal);
                break;
            default:
                //on riscv, devices external to the cpu will all trigger an external
                //interrupt. If we get an interrupt number other than these three
                //something has gone wrong.
                ASSERT_UNREACHABLE();
            }
            
            ClearCsrBits("sip", 1 << frame->vector);
        }
        else if (frame->vector >= 12 && frame->vector <= 15)
        {
            using Memory;
            VmFaultFlags flags {};
            if (frame->vector == 12)
                flags |= VmFaultFlag::Execute;
            else if (frame->vector == 13)
                flags |= VmFaultFlag::Read;
            else if (frame->vector == 15)
                flags |= VmFaultFlag::Write;
            
            if (frame->flags.spp == 0)
                flags |= VmFaultFlag::User;
            if (frame->ec < hhdmBase)
                VMM::Current().HandleFault(frame->ec, flags);
            else
                VMM::Kernel().HandleFault(frame->ec, flags);
        }
        else if (frame->vector < 12)
        {
            Log("Unexpected exception: %s (%lu) @ 0x%lx, sp=0x%lx, ec=0x%lx", LogLevel::Fatal, 
                exceptionNames[frame->vector], frame->vector, frame->sepc, frame->sp, frame->ec);
        }
        else
            Log("Unknown CPU exception: %lu", LogLevel::Fatal, frame->vector);

        Tasking::Scheduler::Global().Yield();
        CoreLocal().runLevel = prevRunLevel;
        SwitchFrame(nullptr, frame);
    }
}
