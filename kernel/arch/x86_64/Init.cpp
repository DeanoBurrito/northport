#include <arch/Init.h>
#include <arch/Misc.h>
#include <core/Log.h>

namespace Npk
{
#ifdef NPK_X86_DEBUGCON_ENABLED
    static void DebugconWrite(sl::StringSpan text)
    {
        for (size_t i = 0; i < text.Size(); i++)
            Out8(Npk::PortDebugcon, text[i]);
    }

    Core::LogOutput debugconOutput
    {
        .Write = DebugconWrite,
        .BeginPanic = nullptr
    };
#endif

    static uint64_t gdtEntries[7] = 
    {
        0,                      //0x00: null selector
        0x00AF'9B00'0000'FFFF,  //0x08: kernel code
        0x00AF'9300'0000'FFFF,  //0x10: kernel data
        0x00AF'F300'0000'FFFF,  //0x18: user data
        0x00AF'FB00'0000'FFFF,  //0x20: user code
        0,                      //0x28: tss low
        0,                      //0x30: tss high
    };

    static struct PACKED_STRUCT
    {
        uint16_t limit;
        uint64_t base;
    } gdtr;

    constexpr size_t IdtEntryCount = 256;
    extern uint8_t VectorStub0[] asm("VectorStub0");

    struct IdtEntry
    {
        uint64_t low;
        uint64_t high;
    };

    IdtEntry idtEntries[IdtEntryCount];

    static struct PACKED_STRUCT
    {
        uint16_t limit;
        uint64_t base;
    } idtr;

    NAKED_FUNCTION
    void LoadGdt()
    {
        asm volatile("lgdt %0" :: "m"(gdtr));

        asm volatile("mov %0, %%ax;  \
            mov %%ax, %%ds; \
            mov %%ax, %%ss; \
            mov %%ax, %%es; \
            mov %%ax, %%fs; \
            pop %%rdi; \
            push %1; \
            push %%rdi; \
            lretq " 
            :: "g"(SelectorKernelData), "g"(SelectorKernelCode)
            : "rdi");
    }

    NAKED_FUNCTION
    void LoadIdt()
    {
        asm("lidt %0; ret" :: "m"(idtr));
    }

    void ArchKernelEntry()
    {
#ifdef NPK_X86_DEBUGCON_ENABLED
        Core::AddLogOutput(&debugconOutput);
#endif

        WriteMsr(MsrGsBase, 0);

        gdtr.limit = sizeof(gdtEntries) - 1;
        gdtr.base = (uint64_t)gdtEntries;
        idtr.limit = sizeof(idtEntries) - 1;
        idtr.base = (uint64_t)idtEntries;

        for (size_t i = 0; i < IdtEntryCount; i++)
        {
            const uintptr_t addr = (uintptr_t)VectorStub0 + i * 16;

            idtEntries[i].low = (addr & 0xFFFF) | ((addr & 0xFFFF'0000) << 32);
            idtEntries[i].low |= SelectorKernelCode << 16;
            idtEntries[i].low |= ((0b1110ull << 8) | (1ull <<15)) << 32;
            idtEntries[i].high = addr >> 32;
        }
    }

    void ArchLateKernelEntry()
    { ASSERT_UNREACHABLE(); }

    void ArchInitCore(size_t myId)
    { ASSERT_UNREACHABLE(); }

    void ArchThreadedInit()
    { ASSERT_UNREACHABLE(); }
}
