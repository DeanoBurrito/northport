#include <arch/riscv64/Interrupts.h>
#include <arch/Platform.h>
#include <debug/Log.h>
#include <interrupts/InterruptManager.h>
#include <stdint.h>

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

    extern void (*timerCallback)(size_t);
}

extern "C"
{
    void* TrapDispatch(Npk::TrapFrame* frame)
    {
        using namespace Npk;
        const bool isInterrupt = frame->vector & (1ul << 63);
        frame->vector &= ~(1ul << 63);

        if (isInterrupt)
        {
            switch (frame->vector)
            {
            case 1:
                Log("Got riscv IPI.", LogLevel::Fatal);
                break;
            case 5:
                if (timerCallback)
                    timerCallback(5);
                break;
            case 9:
                Log("Got riscv external interrupt.", LogLevel::Fatal);
                break;
            default:
                Interrupts::InterruptManager::Global().Dispatch(frame->vector);
                break;
            }
        }
        else
            Log("Native CPU exception: 0x%lx.", LogLevel::Fatal, frame->vector);

        return frame;
    }
}
