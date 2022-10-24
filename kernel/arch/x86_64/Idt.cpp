#include <arch/x86_64/Idt.h>
#include <arch/x86_64/Apic.h>
#include <arch/x86_64/Gdt.h>
#include <debug/Log.h>
#include <interrupts/InterruptManager.h>
#include <interrupts/Ipi.h>

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
    void* TrapDispatch(Npk::TrapFrame* frame)
    {
        using namespace Npk;
        RunLevel prevRunLevel = CoreLocal().runLevel;
        CoreLocal().runLevel = RunLevel::IntHandler;

        if (frame->vector < 0x20)
            Log("Native CPU exception: 0x%lx", LogLevel::Fatal, frame->vector);
        else if (frame->vector == IntVectorIpi)
            Interrupts::ProcessIpiMail(); //TODO: move this to a dpc
        else
            Interrupts::InterruptManager::Global().Dispatch(frame->vector);

        LocalApic::Local().SendEoi();
        CoreLocal().runLevel = prevRunLevel;
        return frame;
    }
}
