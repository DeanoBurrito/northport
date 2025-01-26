#include <arch/Init.h>
#include <arch/Misc.h>
#include <arch/x86_64/Apic.h>
#include <arch/x86_64/Cpuid.h>
#include <arch/x86_64/Timers.h>
#include <core/Log.h>
#include <core/WiredHeap.h>
#include <core/Config.h>

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

    static struct SL_PACKED(
    {
        uint16_t limit;
        uint64_t base;
    }) gdtr;

    constexpr size_t IdtEntryCount = 256;
    extern uint8_t VectorStub0[] asm("VectorStub0");

    struct IdtEntry
    {
        uint64_t low;
        uint64_t high;
    };

    IdtEntry idtEntries[IdtEntryCount];

    static struct SL_PACKED(
    {
        uint16_t limit;
        uint64_t base;
    }) idtr;

    SL_NAKED_FUNC
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

    SL_NAKED_FUNC
    void LoadIdt()
    {
        asm("lidt %0; ret" :: "m"(idtr));
    }

    SL_NO_KASAN
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

        LoadGdt();
        LoadIdt();
    }

    void ArchLateKernelEntry()
    {
        InitIoApics();
        CalibrationTimersInit();
    }

    void ArchInitCore(size_t myId)
    { 
        //TODO: sync MTRRs
        LoadGdt();
        LoadIdt();

        CoreLocalBlock* clb = NewWired<CoreLocalBlock>();
        ASSERT_(clb != nullptr);
        WriteMsr(MsrGsBase, reinterpret_cast<uint64_t>(clb));
        clb->id = myId;
        clb->rl = RunLevel::Normal;
        clb->xsaveBitmap = 0;
        clb->xsaveSize = 0;

        for (size_t i = 0; i < static_cast<size_t>(SubsysPtr::Count); i++)
            clb->subsysPtrs[i] = nullptr;

        //NOTE: x86_64 mandates the fpu, sse and sse2, so no need to check for them.
        ASSERT_(CpuHasFeature(CpuFeature::FxSave));

        uint64_t cr0 = ReadCr0();
        cr0 |= 1 << 1; //MP: monitor co-processor
        cr0 &= ~(3 << 2); //clear TS (task switched) and EM (emulate co processor)
        cr0 |= 1 << 5; //enable cpu exceptions
        cr0 |= 1 << 16; //write-protect bit
        cr0 &= ~0x6000'0000; //ensure caches are enabled for this core
        WriteCr0(cr0);

        if (Core::GetConfigNumber("kernel.boot.print_cpu_features", false))
        {
            Log("Dumping cpuid values:", LogLevel::Verbose);
            LogCpuFeatures();
        }

        uint64_t cr4 = ReadCr4();
        cr4 |= 3 << 9; //enable fxsave support and vector unit exceptions
        if (CpuHasFeature(CpuFeature::Smap))
            cr4 |= 1 << 21;
        if (CpuHasFeature(CpuFeature::Smep))
            cr4 |= 1 << 20;
        if (CpuHasFeature(CpuFeature::Umip))
            cr4 |= 1 << 11;
        if (CpuHasFeature(CpuFeature::GlobalPages))
            cr4 |= 1 << 7;
        WriteCr4(cr4);

        if (CpuHasFeature(CpuFeature::NoExecute))
            WriteMsr(MsrEfer, ReadMsr(MsrEfer) | (1 << 11));

        if (cr4 & (1 << 21))
            asm volatile("clac"); //prevent accidental userspace accesses
        asm("finit");

        if (CpuHasFeature(CpuFeature::XSave))
        {
            cr4 |= 1 << 18; //enable xsave suite of instructions
            WriteCr4(cr4);

            CpuidLeaf leaf {};
            DoCpuid(0xD, 0, leaf);
            //NOTE: higher bits of xsave are for features we dont currently support
            //(tileconfig and memory protection keys)
            clb->xsaveBitmap = leaf.a & 0xFF;

            asm("xsetbv" :: "a"(clb->xsaveBitmap), "d"(clb->xsaveBitmap >> 32), "c"(0));
            DoCpuid(0xD, 0, leaf);
            clb->xsaveSize = leaf.b; //ebx = save area size of xcr0, ecx = max save area size

            Log("Xsave enabled, bitmap=0x%lx, size=%lu B", LogLevel::Verbose,
                clb->xsaveBitmap, clb->xsaveSize);
        }

        LocalApic* lapic = NewWired<LocalApic>();
        ASSERT_(lapic != nullptr);
        ASSERT_(lapic->Init()); //TODO: support PIC?
        SetLocalPtr(SubsysPtr::IntrCtrl, lapic);
    }

    void ArchThreadedInit()
    { ASSERT_UNREACHABLE(); }
}
