#include <arch/x86_64/Apic.h>
#include <arch/x86_64/Idt.h>
#include <arch/x86_64/Cpuid.h>
#include <boot/CommonInit.h>
#include <debug/Log.h>

namespace Npk
{
#ifdef NPK_X86_DEBUGCON_ENABLED
    static void DebugconWrite(sl::StringSpan text)
    {
        for (size_t i = 0; i < text.Size(); i++)
            Npk::Out8(Npk::PortDebugcon, text[i]);
    }

    Npk::Debug::LogOutput debugconOutput
    {
        .Write = DebugconWrite,
        .BeginPanic = nullptr
    };
#endif

    [[gnu::aligned(8)]]
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

    static struct [[gnu::packed, gnu::aligned(8)]]
    {
        uint16_t limit;
        uint64_t base;
    } gdtr;

    [[gnu::naked]]
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

    void ArchKernelEntry()
    {
#ifdef NPK_X86_DEBUGCON_ENABLED
        Debug::AddLogOutput(&debugconOutput);
#endif

        WriteMsr(MsrGsBase, 0);
        gdtr.limit = sizeof(gdtEntries) - 1;
        gdtr.base = (uint64_t)gdtEntries;

        PopulateIdt();
    }

    void ArchLateKernelEntry()
    {
        IoApic::InitAll();
    }

    void ArchInitCore(size_t myId)
    {
        LoadGdt();
        LoadIdt();

        CoreLocalInfo* clb = new CoreLocalInfo();
        WriteMsr(MsrGsBase, reinterpret_cast<uintptr_t>(clb));
        clb->subsystemPtrs[(size_t)LocalPtr::IntrControl] = new LocalApic();
        clb->id = myId;
        clb->runLevel = RunLevel::Dpc;

        uint64_t cr0 = ReadCr0();
        cr0 |= 1 << 16; //set write-protect bit
        cr0 &= ~0x6000'0000; //ensure CD/NW are cleared, enabling caching for this core.
        WriteCr0(cr0);

        uint64_t cr4 = ReadCr4();
        if (CpuHasFeature(CpuFeature::Smap))
            cr4 |= 1 << 21; //SMA enable: prevent accessing user pages as the supervisor while AC is clear
        if (CpuHasFeature(CpuFeature::Smep))
            cr4 |= 1 << 20; //Prevent executing from user pages while in ring 0.
        if (CpuHasFeature(CpuFeature::Umip))
            cr4 |= 1 << 11; //prevents system store instructions in user mode (sidt/sgdt)
        if (CpuHasFeature(CpuFeature::GlobalPages))
            cr4 |= 1 << 7; //global pages: pages that are global.
        WriteCr4(cr4);

        if (CpuHasFeature(CpuFeature::NoExecute))
            WriteMsr(MsrEfer, ReadMsr(MsrEfer) | (1 << 11));

        //my stack overfloweth (alt title: maximum maintainability).
        Log("Cpu features enabled: wp%s%s%s%s%s.", LogLevel::Info, 
            cr4 & (1 << 21) ? ", smap" : "",
            cr4 & (1 << 20) ? ", smep" : "",
            cr4 & (1 << 11) ? ", umip" : "",
            cr4 & (1 << 7) ? ", gbl-pages" : "",
            CpuHasFeature(CpuFeature::NoExecute) ? ", nx" : "");

        //TODO: init fpu and sse state

        LocalApic::Local().Init();
    }

    void ArchThreadedInit()
    {} //TODO: add port IO access to npk_add_bus_access()
}
