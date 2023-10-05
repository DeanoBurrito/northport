#include <arch/x86_64/Idt.h>
#include <arch/x86_64/Apic.h>
#include <arch/x86_64/Gdt.h>
#include <debug/Log.h>
#include <interrupts/InterruptManager.h>
#include <interrupts/Ipi.h>
#include <memory/Vmm.h>
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

    void PopulateIdt()
    {
        for (size_t i = 0; i < IntVectorCount; i++)
            idtEntries[i] = IdtEntry((uintptr_t)VectorStub0 + i * 0x10, false, 0);
    }

    struct [[gnu::packed, gnu::aligned(8)]]
    {
        uint16_t limit = IntVectorCount * sizeof(IdtEntry) - 1;
        uint64_t base = (uintptr_t)idtEntries;
    } idtr;

    [[gnu::naked]]
    void LoadIdt()
    {
        asm("lidt %0; ret" :: "m"(idtr));
    }
}

extern "C"
{
    constexpr const char* exceptionNames[] = 
    {
        "divide error",
        "debug exception",
        "nmi",
        "breakpoint",
        "overflow",
        "BOUND range exceeded",
        "invalid opcode",
        "device not available",
        "double fault",
        "fpu segment overrun",
        "invalid TSS",
        "segment not present",
        "stack-segment fault",
        "general protection fault",
        "page fault",
        "you got the reserved fault vector, have fun debugging",
        "fpu math fault",
        "alignment check",
        "machine check",
        "SIMD exception",
        "virtualization exception",
        "control protection"
    };
    
    void HandleNativeException(Npk::TrapFrame* frame)
    {
        using namespace Npk;

        if (frame->vector == 0xE)
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

            bool success = false;
            const uintptr_t cr2 = ReadCr2();
            if (cr2 < hhdmBase)
                success = VMM::Current().HandleFault(cr2, flags);
            else
                success = VMM::Kernel().HandleFault(cr2, flags);
            if (!success)
                Log("Bad page fault @ 0x%lx, flags=0x%lx", LogLevel::Fatal, cr2, flags.Raw());
        }
        else if (frame->vector == 0x7)
            Tasking::Scheduler::Global().SwapExtendedRegs();
        else
            Log("Unexpected exception: %s (%lu) @ 0x%lx, sp=0x%lx, ec=0x%lx", LogLevel::Fatal, 
                exceptionNames[frame->vector], frame->vector, frame->iret.rip, frame->iret.rsp, frame->ec);
    }
    
    void TrapDispatch(Npk::TrapFrame* frame)
    {
        using namespace Npk;

        const RunLevel prevRunLevel = CoreLocal().runLevel;
        CoreLocal().runLevel = RunLevel::IntHandler;
        Tasking::Scheduler::Global().SavePrevFrame(frame, prevRunLevel);
        
        if (frame->vector < 0x20)
            HandleNativeException(frame);
        else
        {
            LocalApic::Local().SendEoi();
            if (frame->vector == IntVectorIpi)
                Interrupts::ProcessIpiMail();
            else
                Interrupts::InterruptManager::Global().Dispatch(frame->vector);
        }

        //RunNextFrame() wont return under most circumstances, but if we're handling an interrupt
        //before the scheduler is initialized (timekeeping for example) it will fail to find the
        //trap frame, so we return to where we were previously.
        Tasking::Scheduler::Global().Yield();
        CoreLocal().runLevel = prevRunLevel;
        SwitchFrame(nullptr, frame);
    }
}
