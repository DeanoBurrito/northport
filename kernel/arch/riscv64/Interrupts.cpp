#include <arch/riscv64/Interrupts.h>
#include <arch/riscv64/Sbi.h>
#include <arch/riscv64/Aia.h>
#include <debug/Log.h>
#include <debug/Panic.h>
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

    extern bool (*timerCallback)(void*);
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

    static Npk::Tasking::ProgramExceptionType ClassifyException(Npk::TrapFrame* frame)
    {
        using ExType = Npk::Tasking::ProgramExceptionType;

        switch (frame->vector)
        {
        case 2: return ExType::InvalidInstruction;
        case 3: return ExType::Breakpoint;
        case 12: case 13: case 14: return ExType::MemoryAccess;
        default: return ExType::BadOperation;
        }
    }

    static void HandleException(Npk::TrapFrame* frame)
    {
        using namespace Npk;
        using namespace Npk::Memory;
        using namespace Npk::Tasking;

        ProgramException exception {};
        exception.instruction = frame->sepc;
        exception.stack = frame->sp;
        exception.special = 0;
        exception.type = ClassifyException(frame);

        if (frame->vector >= 12 && frame->vector < 14)
        {
            using namespace Npk::Memory;
            VmFaultFlags faultFlags {};
            switch (frame->vector)
            {
            case 12: faultFlags |= VmFaultFlag::Execute; break;
            case 13: faultFlags |= VmFaultFlag::Read; break;
            case 14: faultFlags |= VmFaultFlag::Write; break;
            }

            if (frame->flags.spp == 0)
                faultFlags |= VmFaultFlag::User;

            exception.flags = faultFlags.Raw();
            exception.special = frame->ec;

            bool handled = false;
            if (frame->ec < hhdmBase && VMM::CurrentActive())
                handled = VMM::Current().HandleFault(frame->ec, faultFlags);
            else if (frame->ec > hhdmBase)
                handled = VMM::Kernel().HandleFault(frame->ec, faultFlags);

            if (!handled && !ProgramManager::Global().ServeException(exception))
                Debug::PanicWithException(exception, frame->fp);
        }
        else
            Debug::PanicWithException(exception, frame->fp);
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
                ClearCsrBits("sip", 1); //sip.ssip is the only one we can clear ourselves.
                Interrupts::ProcessIpiMail();
                break;
            case 5: //Timer
                SbiSetTimer(-1ul); //clear sip.stip by re-arming timer far into the future.
                if (timerCallback != nullptr)
                    timerCallback(nullptr);
                break;
            case 9: //external interrupt, sip.seip must be cleared by the ext interrupt controller.
                HandleAiaInterrupt();
                break;
            default:
                ASSERT_UNREACHABLE();
            };
        }
        else
            HandleException(frame);
        
        LowerRunLevel(prevRl);
        DisableInterrupts();
        SwitchFrame(nullptr, frame);
        ASSERT_UNREACHABLE();
    }
}
