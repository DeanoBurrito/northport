#include <arch/m68k/Interrupts.h>
#include <arch/m68k/GfPic.h>
#include <arch/Platform.h>
#include <debug/Log.h>
#include <debug/Panic.h>
#include <tasking/Threads.h>
#include <Locks.h>

namespace Npk
{
    sl::SpinLock vectorTableLock;
    uint32_t* vectorTable = nullptr;

    void TrapEntry() asm("TrapEntry"); //defined in Trap.S

    void LoadVectorTable()
    {
        vectorTableLock.Lock();
        if (vectorTable == nullptr)
        {
            vectorTable = new uint32_t[IntVectorAllocLimit];
            for (size_t i = 0; i < IntVectorAllocLimit; i++)
                vectorTable[i] = reinterpret_cast<uintptr_t>(&TrapEntry);

            Log("Vector table allocated at %p", LogLevel::Verbose, vectorTable);
        }
        vectorTableLock.Unlock();

        asm volatile("movec %0, %%vbr" :: "d"(vectorTable));
    }
}

extern "C"
{
    constexpr size_t VectorAccessFault = 0x2;
    constexpr size_t VectorAddressError = 0x3;
    constexpr size_t VectorIllegalInstr = 0x4;
    constexpr size_t VectorTrace = 0x9;

    static Npk::Tasking::ProgramExceptionType ClassifyException(Npk::TrapFrame* frame)
    {
        using ExType = Npk::Tasking::ProgramExceptionType;

        switch (frame->rte.vector)
        {
        case VectorTrace: return ExType::Breakpoint;
        case VectorIllegalInstr: return ExType::InvalidInstruction;
        case VectorAccessFault: return ExType::MemoryAccess;
        case VectorAddressError: return ExType::MemoryAccess;
        default: return ExType::BadOperation;
        }
    }

    static void HandleException(Npk::TrapFrame* frame)
    {
        using namespace Npk;
        using namespace Npk::Tasking;
        using namespace Npk::Debug;

        ProgramException exception {};
        exception.instruction = frame->rte.pc;
        exception.stack = frame->a7;
        exception.special = frame->rte.vector;
        exception.flags = 0;
        exception.type = ClassifyException(frame);

        if (frame->rte.vector == VectorAccessFault)
        {
            using namespace Memory;
            VmFaultFlags flags {};
            if (frame->rte.format7.ssw & (1 << 8))
                flags |= VmFaultFlag::Read;
            else
                flags |= VmFaultFlag::Write;
            if ((frame->rte.sr & (1 << 13)) == 0)
                flags |= VmFaultFlag::User;
            //NOTE: we dont try setting the execute flag, as the m68k mmu has no execute protection.
            //Even if we could detect it here, a page fault wont be triggered on a bad fetch.

            exception.flags = flags.Raw();
            exception.special = frame->rte.format7.faultAddr;

            bool handled = false;
            if (exception.special < hhdmBase && VMM::CurrentActive())
                handled = VMM::Current().HandleFault(exception.special, flags);
            else if (exception.special >= hhdmBase)
                handled = VMM::Kernel().HandleFault(exception.special, flags);

            if (!handled && !ProgramManager::Global().ServeException(exception))
                PanicWithException(exception, frame->a6);

        }
        else if (!ProgramManager::Global().ServeException(exception))
            PanicWithException(exception, frame->a6);
    }

    void TrapDispatch(Npk::TrapFrame* frame)
    {
        using namespace Npk;
        using namespace Npk::Tasking;

        const RunLevel prevRl = RaiseRunLevel(RunLevel::Interrupt);
        if (prevRl == RunLevel::Normal)
            *ProgramManager::Global().GetCurrentFrameStore() = frame;

        frame->rte.vector >>= 2; //we want vector number, not offset
        if (frame->rte.vector >= 25 && frame->rte.vector <= 31)
            HandlePicInterrupt(frame->rte.vector);
        else
            HandleException(frame);

        LowerRunLevel(prevRl);
        SwitchFrame(nullptr, frame);
        ASSERT_UNREACHABLE();
    }
}
