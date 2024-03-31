#include <arch/riscv64/Interrupts.h>
#include <arch/riscv64/Sbi.h>
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
    constexpr const char* ExceptionStrs[] = 
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
    constexpr size_t ExceptionStrCount = sizeof(ExceptionStrs) / sizeof(const char*);

    constexpr uintptr_t InterruptBitMask = 1ul << 63;
    constexpr size_t VectorExecPf = 12;
    constexpr size_t VectorReadPf = 13;
    constexpr size_t VectorWritePf = 14;

    void HandleException(Npk::TrapFrame* frame)
    {
        using namespace Npk;
        using namespace Npk::Memory;

        Memory::VmFaultFlags faultFlags {};
        switch (frame->vector)
        {
        case VectorExecPf:
            faultFlags |= VmFaultFlag::Execute;
            break;
        case VectorReadPf:
            faultFlags |= VmFaultFlag::Read;
            break;
        case VectorWritePf:
            faultFlags |= VmFaultFlag::Write;
            break;
        //TOOD: UD and decoder for FPU/vector register traps

        default:
            {
                const char* exceptionName = frame->vector < ExceptionStrCount 
                    ? ExceptionStrs[frame->vector] : "unknown";
                Log("Unexpected exception: %s (%lu) @ 0x%lx, sp=0x%lx, ec=0x%lx", LogLevel::Fatal,
                   exceptionName, frame->vector, frame->sepc, frame->sp, frame->ec);
            }
        };

        ASSERT_(faultFlags.Any());
        if (frame->ec < hhdmBase)
            ASSERT(VMM::CurrentActive(), "nullptr deref in kernel?");
        if (frame->flags.spp == 0)
            faultFlags |= VmFaultFlag::User;

        bool success = false;
        if (frame->ec < hhdmBase)
        {
            ASSERT(VMM::CurrentActive(), "nullptr deref in kernel?");
            success = VMM::Current().HandleFault(frame->ec, faultFlags);
        }
        else
            success = VMM::Kernel().HandleFault(frame->ec, faultFlags);
        if (!success)
            Log("Bad page fault @ 0x%lx, flags=0x%lx", LogLevel::Fatal, frame->ec, faultFlags.Raw());
    }
    
    void TrapDispatch(Npk::TrapFrame* frame)
    {
        using namespace Npk;
        using namespace Npk::Tasking;

        const RunLevel prevRl = RaiseRunLevel(RunLevel::Interrupt);
        if (prevRl == RunLevel::Normal)
            *ProgramManager::Global().GetCurrentFrameStore() = frame;
        EnableInterrupts();

        const bool isInterrupt = frame->vector & InterruptBitMask;
        frame->vector &= ~InterruptBitMask;

        if (isInterrupt)
        {
            switch (frame->vector)
            {
            case 1: //IPI
                Interrupts::ProcessIpiMail();
                ClearCsrBits("sip", 1); //sip.ssip is the only one we can clear ourselves
                break;
            case 5: //Timer (TODO: clear using EE)
                SbiSetTimer(-1ul);
                if (timerCallback != nullptr)
                    timerCallback();
            case 9: //external interrupt (SIP bit must be cleared by interrupt controller)
            default:
                ASSERT_UNREACHABLE();
            };
        }
        else
            HandleException(frame);
        
        if (prevRl == RunLevel::Normal)
            frame = *ProgramManager::Global().GetCurrentFrameStore();
        LowerRunLevel(prevRl);
        DisableInterrupts();
        SwitchFrame(nullptr, frame);
        ASSERT_UNREACHABLE();
    }
}
