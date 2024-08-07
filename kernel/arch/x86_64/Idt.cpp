#include <arch/x86_64/Idt.h>
#include <arch/x86_64/Apic.h>
#include <debug/Log.h>
#include <debug/Panic.h>
#include <io/IntrRouter.h>
#include <interrupts/Ipi.h>
#include <memory/Vmm.h>
#include <tasking/RunLevels.h>
#include <tasking/Scheduler.h>

namespace Npk
{
    extern uint8_t TrapEntry[] asm("TrapEntry");
    extern uint8_t VectorStub0[] asm("VectorStub0");

    struct IdtEntry
    {
        uint64_t low;
        uint64_t high;

        IdtEntry() = default;

        IdtEntry(uint64_t addr, bool syscall, size_t ist)
        {
            low = (addr & 0xFFFF) | ((addr & 0xFFFF'0000) << 32);
            high = addr >> 32;
            low |= SelectorKernelCode << 16;
            low |= ((ist & 0b11) | (0b1110 << 8) | (syscall ? (3 << 13) : (0 << 13)) | (1 << 15)) << 32;
        }
    };

    [[gnu::aligned(8)]]
    IdtEntry idtEntries[IntVectorCount];

    struct [[gnu::packed, gnu::aligned(8)]]
    {
        uint16_t limit;
        uint64_t base;
    } idtr;

    void PopulateIdt()
    {
        idtr.limit = sizeof(idtEntries) - 1;
        idtr.base = (uintptr_t)idtEntries;

        for (size_t i = 0; i < IntVectorCount; i++)
            idtEntries[i] = IdtEntry((uintptr_t)VectorStub0 + i * 0x10, false, 0);
    }


    [[gnu::naked]]
    void LoadIdt()
    {
        asm("lidt %0; ret" :: "m"(idtr));
    }
}

extern "C"
{
    constexpr size_t VectorDebug = 0x1;
    constexpr size_t VectorInvalidOpcode = 0x6;
    constexpr size_t VectorExtStateAccess = 0x7;
    constexpr size_t VectorPageFault = 0xE;

    static Npk::Tasking::ProgramExceptionType ClassifyException(Npk::TrapFrame* frame)
    {
        using ExType = Npk::Tasking::ProgramExceptionType;

        switch (frame->vector)
        {
        case VectorDebug: return ExType::Breakpoint;
        case VectorInvalidOpcode: return ExType::InvalidInstruction;
        case VectorPageFault: return ExType::MemoryAccess;
        default: return ExType::BadOperation;
        }
    }
    
    static void HandleException(Npk::TrapFrame* frame)
    {
        using namespace Npk;
        using namespace Npk::Tasking;
        using namespace Npk::Debug;

        ProgramException exception {};
        exception.instruction = frame->iret.rip;
        exception.stack = frame->iret.rsp;
        exception.special = frame->vector;
        exception.flags = frame->ec;
        exception.type = ClassifyException(frame);

        if (frame->vector == VectorPageFault)
        {

            using namespace Memory;
            VmFaultFlags flags {};
            if (frame->ec & (1 << 1))
                flags |= VmFaultFlag::Write;
            if (frame->ec & (1 << 4))
                flags |= VmFaultFlag::Execute;
            if (!flags.Any())
                flags |= VmFaultFlag::Read;
            
            if (frame->ec & (1 << 2))
                flags |= VmFaultFlag::User;

            const uintptr_t cr2 = ReadCr2();
            exception.flags = flags.Raw();
            exception.special = cr2;

            //figure out who to notify of the page fault
            bool handled = false;
            if (cr2 < hhdmBase && VMM::CurrentActive())
                handled = VMM::Current().HandleFault(cr2, flags);
            else if (cr2 > hhdmBase)
                handled = VMM::Kernel().HandleFault(cr2, flags);
            
            if (!handled && !ProgramManager::Global().ServeException(exception))
                PanicWithException(exception, frame->rbp);
        }
        else if (frame->vector == VectorExtStateAccess)
            Tasking::Scheduler::Global().SwapExtendedRegs();
        else
            PanicWithException(exception, frame->rbp);
    }
    
    void TrapDispatch(Npk::TrapFrame* frame)
    {
        using namespace Npk;
        using namespace Npk::Tasking;

        const RunLevel prevRl = RaiseRunLevel(RunLevel::Interrupt);
        if (prevRl == RunLevel::Normal)
            *ProgramManager::Global().GetCurrentFrameStore() = frame;
        EnableInterrupts();

        if (frame->vector < 0x20)
            HandleException(frame);
        else
        {
            LocalApic::Local().SendEoi();
            if (frame->vector == IntVectorIpi)
                Interrupts::ProcessIpiMail();
            else
                Io::InterruptRouter::Global().Dispatch(frame->vector);
        }

        LowerRunLevel(prevRl);
        DisableInterrupts();
        SwitchFrame(nullptr, frame);
        ASSERT_UNREACHABLE();
    }
}
