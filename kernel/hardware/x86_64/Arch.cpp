#include <hardware/Arch.h>
#include <hardware/x86_64/Apic.h>
#include <hardware/x86_64/Cpuid.h>
#include <core/Config.h>
#include <core/Log.h>
#include <core/Smp.h>
#include <core/WiredHeap.h>
#include <core/Acpi.h>
#include <Maths.h>
#include <Memory.h>

#define FXSAVE(regs) do { asm("fxsave %0" :: "m"(regs) : "memory"); } while (false)
#define FXRSTOR(regs) do { asm("fxrstor %0" :: "m"(regs)); } while (false)
#define XSAVE(regs, bitmap) do { asm("xsave %0" :: "m"(regs), \
    "a"(bitmap & 0xFFFF'FFFF), "d"(bitmap >> 32) : "memory"); } while (false)
#define XRSTOR(regs, bitmap) do { asm("xrstor %0" :: "m"(regs), \
    "a"(bitmap & 0xFFFF'FFFF), "d"(bitmap >> 32)); } while (false)

asm(R"(
.global SwitchExecFrame

.type SwitchExecFrame, @function
.size SwitchExecFrame, (_EndOfSwitchExecFrame - SwitchExecFrame)

.pushsection .text
SwitchExecFrame:
    test %rdi, %rdi ## skip saving current state if `store` is null
    jz 1f

    ## rip is already on the stack from calling this function
    pushfq
    push %r15
    push %r14
    push %r13
    push %r12
    push %rbp
    push %rbx
    push %rdi

    mov %rsp, (%rdi)
1:
## switch stacks, run callback if non-null
    mov %rsi, %rsp
    test %rdx, %rdx
    jz 1f

    mov %rcx, %rdi
    call *%rdx
1:
## load next state
    pop %rdi
    pop %rbx
    pop %rbp
    pop %r12
    pop %r13
    pop %r14
    pop %r15
    popfq
    ret
_EndOfSwitchExecFrame:
.popsection
)");

namespace Npk
{
    constexpr uint64_t Cr0TsFlag = 1 << 3;
    constexpr size_t FxsaveSize = 512;
    
    struct ExtendedRegs
    {
        uint8_t buffer[0]; //size of this buffer is stored in CoreLocalBlock().xsaveSize
    };

#ifdef NPK_X86_ASSUME_DEBUGCON
    static void DebugconWrite(sl::StringSpan text)
    {
        for (size_t i = 0; i < text.Size(); i++)
            Out8(PortDebugcon, text[i]);
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
    extern uint8_t InterruptStubsBegin[] asm("InterruptStubsBegin");

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

    uintptr_t lapicMmioBase;
    uintptr_t hpetMmioBase;

    void CalibrationTimersInit(uintptr_t hpetMmioBase); //defined in Timers.cpp

    SL_NAKED_FUNC
    void LoadGdt()
    {
        asm volatile("lgdt %0" :: "m"(gdtr));

        asm volatile("mov %0, %%ax;  \
            mov %%ax, %%ds; \
            mov %%ax, %%ss; \
            mov %%ax, %%es; \
            mov $0, %%ax; \
            mov %%ax, %%fs; \
            mov %%ax, %%gs; \
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

    void ArchEarlyEntry()
    {
#ifdef NPK_X86_ASSUME_DEBUGCON
        Core::AddLogOutput(&debugconOutput);
#endif
        WriteMsr(Msr::GsBase, 0);

        gdtr.limit = sizeof(gdtEntries) - 1;
        gdtr.base = (uint64_t)gdtEntries;
        idtr.limit = sizeof(idtEntries) - 1;
        idtr.base = (uint64_t)idtEntries;

        for (size_t i = 0; i < IdtEntryCount; i++)
        {
            const uintptr_t addr = (uintptr_t)InterruptStubsBegin + i * 16;

            idtEntries[i].low = (addr & 0xFFFF) | ((addr & 0xFFFF'0000) << 32);
            idtEntries[i].low |= SelectorKernelCode << 16;
            idtEntries[i].low |= ((0b1110ull << 8) | (1ull <<15)) << 32;
            idtEntries[i].high = addr >> 32;
        }

        LoadGdt();
        LoadIdt();
    }

    void ArchMappingEntry(EarlyMmuEnvironment& env, uintptr_t& vaddrAlloc)
    {
        lapicMmioBase = vaddrAlloc;
        vaddrAlloc += Core::smpInfo.cpuCount << PfnShift();

        if (Core::AcpiTableExists(Core::SigHpet))
        {
            hpetMmioBase = vaddrAlloc;
            vaddrAlloc += PageSize();
        }

        //TODO: reserve space for io apics
    }

    void ArchLateEntry()
    {
        InitIoApics();
        CalibrationTimersInit(hpetMmioBase);
    }

    void ArchInitCore(size_t id)
    {
        LoadGdt();
        LoadIdt();

        CoreLocalBlock* clb = NewWired<CoreLocalBlock>();
        ASSERT_(clb != nullptr);
        WriteMsr(Msr::GsBase, reinterpret_cast<uint64_t>(clb));
        clb->id = id;
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
        WriteCr4(cr4);

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
        ASSERT_(lapic->Init(lapicMmioBase + (CoreId() << PfnShift())));
        SetLocalPtr(SubsysPtr::IntrCtrl, lapic);

        lapic->CalibrateTimer();
    }

    struct ExecFrame
    {
        uint64_t rdi;
        uint64_t rbx;
        uint64_t rbp;
        uint64_t r12;
        uint64_t r13;
        uint64_t r14;
        uint64_t r15;
        uint64_t flags;
        uint64_t rip;
    };

    ExecFrame* InitExecFrame(uintptr_t stack, uintptr_t entry, void* arg)
    {
        ExecFrame* frame = reinterpret_cast<ExecFrame*>(
            sl::AlignDown(stack - sizeof(ExecFrame), alignof(ExecFrame)));

        frame->rip = entry;
        frame->flags = 0x202;
        frame->rbx = 0;
        frame->rbp = 0;
        frame->r12 = 0;
        frame->r13 = 0;
        frame->r14 = 0;
        frame->r15 = 0;
        frame->rdi = reinterpret_cast<uint64_t>(arg);

        return frame;
    }

    void InitExtendedRegs(ExtendedRegs** regs)
    {
        VALIDATE_(regs != nullptr, );

        const size_t bufferSize = sl::Max(CoreLocalBlock().xsaveSize, FxsaveSize);
        *regs = reinterpret_cast<ExtendedRegs*>(new uint8_t[bufferSize]);
        if (*regs != nullptr)
            sl::MemSet(*regs, 0, bufferSize);
        (void)regs;
    }

    void SaveExtendedRegs(ExtendedRegs* regs)
    {
        VALIDATE_(regs != nullptr, );

        WriteCr0(ReadCr0() & ~Cr0TsFlag);
        if (CoreLocalBlock().xsaveBitmap == 0)
            FXSAVE(*regs);
        else
            XSAVE(*regs, CoreLocalBlock().xsaveBitmap);
    }

    void LoadExtendedRegs(ExtendedRegs* regs)
    {
        VALIDATE_(regs != nullptr, );

        WriteCr0(ReadCr0() & ~Cr0TsFlag);
        if (CoreLocalBlock().xsaveBitmap == 0)
            FXRSTOR(*regs);
        else
            XRSTOR(*regs, CoreLocalBlock().xsaveBitmap);
    }

    bool ExtendedRegsFence()
    {
        WriteCr0(ReadCr0() | Cr0TsFlag);
        return true;
    }

    size_t GetCallstack(sl::Span<uintptr_t> store, uintptr_t start, size_t offset)
    {
        struct Frame
        {
            Frame* next;
            uintptr_t retAddr;
        };

        size_t count = 0;
        Frame* current = reinterpret_cast<Frame*>(start);
        if (start == 0)
            current = static_cast<Frame*>(__builtin_frame_address(0));

        for (size_t i = 0; i < offset; i++)
        {
            if (current == 0)
                return count;
            current = current->next;
        }

        for (size_t i = 0; i < store.Size(); i++)
        {
            if (current == nullptr)
                return count;
            store[count++] = current->retAddr;
            current = current->next;
        }

        return count;
    }

    void PoisonMemory(sl::Span<uint8_t> range)
    {
        //0xCC is `int3` instruction, it generates a breakpoint exception.
        sl::MemSet(range.Begin(), 0xCC, range.Size());
    }
}
