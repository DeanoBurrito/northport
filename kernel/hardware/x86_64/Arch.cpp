#include <hardware/Arch.hpp>
#include <hardware/Entry.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <hardware/x86_64/PortIo.hpp>
#include <hardware/x86_64/Mmu.hpp>
#include <hardware/x86_64/LocalApic.hpp>
#include <Core.hpp>
#include <Memory.hpp>
#include <Maths.hpp>
#include <NanoPrintf.hpp>

constexpr uint8_t Int3Opcode = 0xCC;

extern "C" char SysCallEntry[];
extern "C" char SysEnterEntry[];
extern "C" char BadSysCallEntry[];
extern "C" char InterruptStubsBegin[];
extern "C" char DebugEventEntry[];

namespace Npk
{
    struct TrapFrame 
    {
        uint64_t r15;
        uint64_t r14;
        uint64_t r13;
        uint64_t r12;
        uint64_t r11;
        uint64_t r10;
        uint64_t r9;
        uint64_t r8;
        uint64_t rbp;
        uint64_t rdi;
        uint64_t rsi;
        uint64_t rdx;
        uint64_t rcx;
        uint64_t rbx;
        uint64_t rax;
        uint64_t vector;
        uint64_t ec;

        struct
        {
            uint64_t rip;
            uint64_t cs;
            uint64_t flags;
            uint64_t rsp;
            uint64_t ss;
        } iret;
    };

    struct SwitchFrame
    {
        uint64_t rdi;
        uint64_t rsi;
        uint64_t rbx;
        uint64_t rbp;
        uint64_t r12;
        uint64_t r13;
        uint64_t r14;
        uint64_t r15;
        uint64_t flags;
    };

    static void HandleException(TrapFrame* frame)
    {
        switch (frame->vector)
        {
        case 2: //NMI
        //TODO: handle exception
        }
    }

    extern "C" void InterruptDispatch(TrapFrame* frame)
    {
        auto prevIpl = RaiseIpl(Ipl::Interrupt);

        bool suppressEoi = false;
        switch (frame->vector)
        {
        case LapicSpuriousVector:
            suppressEoi = true;
            break;
        case LapicErrorVector:
            HandleLapicErrorInterrupt();
            break;
        case LapicTimerVector:
            HandleLapicTimerInterrupt();
            break;
        case LapicIpiVector:
            DispatchIpi();
            break;
        default:
            if (frame->vector < 0x20)
            {
                suppressEoi = true;
                HandleException(frame);
            }
            else
                DispatchInterrupt(frame->vector - 0x20);
            break;
        }

        if (!suppressEoi)
            SignalEoi();
        LowerIpl(prevIpl);
    }

    extern "C" void SyscallDispatch()
    {
        NPK_UNREACHABLE();
    }
}

namespace Npk
{
    static const uint64_t gdt[] =
    {
        0,                     //0x00 - null entry, required by spec
        0x00AF'9B00'0000'FFFF, //0x08 - kernel code
        0x00AF'9300'0000'FFFF, //0x10 - kernel data
        0,                     //0x18 - user code 32 (unused)
        0x00AF'FB00'0000'FFFF, //0x20 - user data (for sysret)
        0x00AF'F300'0000'FFFF, //0x28 - user code 64
        0x00AF'FB00'0000'FFFF, //0x30 - user data (for sysexit)
        0, 0                   //0x40-0x50 - TSS
    };
    
    static const struct SL_PACKED(
    {
        uint16_t limit = sizeof(gdt) - 1;
        uint64_t base = reinterpret_cast<uint64_t>(&gdt[0]);
    }) gdtr;

    constexpr size_t IdtSize = 0x100;

    struct IdtEntry
    {
        uint64_t data[2];
    } idt[IdtSize];

    static const struct SL_PACKED(
    {
        uint16_t limit = sizeof(idt) - 1;
        uint64_t base = reinterpret_cast<uint64_t>(&idt[0]);
    }) idtr;

    SL_NAKED_FUNC
    static void LoadGdt()
    {
        asm(R"(
            lgdt %0

            mov $0x10, %%ax
            mov %%ax, %%ds
            mov %%ax, %%ss
            mov %%ax, %%es
            mov %%ax, %%fs

            pop %%rdi
            push $0x08
            push %%rdi
            lretq
            )" :: "m"(gdtr) : "rax", "rdi");
    }

    void CommonCpuSetup()
    {
        LoadGdt();
        //TODO: load TSS

        for (size_t i = 0; i < IdtSize; i++)
        {

            uint64_t addr = (uintptr_t)InterruptStubsBegin + i * 16;
            if (i == DebugEventVector)
                addr = (uintptr_t)DebugEventEntry;
            idt[i].data[0] = (addr & 0xFFFF) | ((addr & 0xFFFF'0000) << 32);
            idt[i].data[0] |= 0x08 << 16; //kernel code selector
            idt[i].data[0] |= ((0b1110ull << 8) | (1ull << 15)) << 32; //present | type
            idt[i].data[1] = addr >> 32;
        }
        asm("lidt %0" :: "m"(idtr));

        uint64_t cr0 = READ_CR(0);
        cr0 &= ~(1 << 30); //clear CD
        cr0 &= ~(1 << 20); //clear NW
        cr0 |= 1 << 16; //set WP
        cr0 |= 1 << 5; //set NE
        cr0 |= 1 << 3; //set TS (tbh it doesnt matter at this stage)
        cr0 &= ~(1 << 2); //clear EM
        cr0 |= 1 << 1; //set MP
        WRITE_CR(0, cr0);

        uint64_t cr4 = READ_CR(4);
        cr4 &= ~(1 << 2); //clear TSD
        if (CpuHasFeature(CpuFeature::DebugExtensions))
            cr4 |= 1 << 3; //set DE
        cr4 &= ~(1 << 6); //clear MCE
        if (CpuHasFeature(CpuFeature::GlobalPages))
            cr4 |= 1 << 7;
        if (CpuHasFeature(CpuFeature::SSE))
        {
            cr4 |= 1 << 9; //set OXFXSR
            cr4 |= 1 << 10; //set OSXMMEXCPT
        }
        if (CpuHasFeature(CpuFeature::Umip))
            cr4 |= 1 << 11;
        if (CpuHasFeature(CpuFeature::WriteFsGsBase))
            cr4 |= 1 << 16; //enable fsgsbase
        if (CpuHasFeature(CpuFeature::XSave))
            cr4 |= 1 << 18; //enable xsave
        if (CpuHasFeature(CpuFeature::Smep))
            cr4 |= 1 << 20; //enable smep
        if (CpuHasFeature(CpuFeature::Smap))
            cr4 |= 1 << 21;
        WRITE_CR(4, cr4);

        uint64_t efer = ReadMsr(Msr::Efer);
        if (CpuHasFeature(CpuFeature::SysCall))
            efer |= 1 << 0; //enable syscall/sysret
        if (CpuHasFeature(CpuFeature::NoExecute))
            efer |= 1 << 11;
        WriteMsr(Msr::Efer, efer);

        Log("Control registers set: cr0=0x%tx, cr4=0x%tx, efer=0x%tx", 
                LogLevel::Verbose, cr0, cr4, efer);

        if (CpuHasFeature(CpuFeature::Smap))
            asm("clac" ::: "memory");

        if (CpuHasFeature(CpuFeature::SysCall))
        {
            WriteMsr(Msr::Star, ((0x18ull | 3) << 48) | (0x8ull << 32));
            WriteMsr(Msr::LStar, reinterpret_cast<uint64_t>(SysCallEntry));
            WriteMsr(Msr::CStar, reinterpret_cast<uint64_t>(BadSysCallEntry));
            WriteMsr(Msr::SFMask, 1 << 9); //will disable intrs on syscall entry

            Log("Setup for syscall/sysret: star=0x%tx, lstar=0x%tx, cstar=0x%tx, sfmask=0x%tx",
                LogLevel::Verbose, ReadMsr(Msr::Star), ReadMsr(Msr::LStar), 
                ReadMsr(Msr::CStar), ReadMsr(Msr::SFMask));
        }

        if (CpuHasFeature(CpuFeature::SysEnter))
        {
            WriteMsr(Msr::SysenterCs, 0x8);
            WriteMsr(Msr::SysenterRip, reinterpret_cast<uint64_t>(SysEnterEntry));
            WriteMsr(Msr::SysenterRsp, 0);

            Log("Setup for sysenter/sysexit: cs=0x%tx, rip=0x%tx, rsp=0x%tx",
                LogLevel::Verbose, ReadMsr(Msr::SysenterCs), ReadMsr(Msr::SysenterRip),
                ReadMsr(Msr::SysenterRsp));
        }

        //TODO: fixups: sse, xsave
    }

    uintptr_t ArchGetTrapReturnAddr(const TrapFrame* frame)
    {
        return frame->iret.rip;
    }

    SL_TAGGED(cpubase, CoreLocalHeader localHeader);

    void SetMyLocals(uintptr_t where, CpuId softwareId)
    {
        auto tls = reinterpret_cast<CoreLocalHeader*>(where);
        tls->swId = softwareId;
        tls->selfAddr = where;
        tls->currThread = nullptr;

        WriteMsr(Msr::GsBase, where);
        Log("Cpu %zu locals at %p", LogLevel::Info, softwareId, tls);
    }

    bool CheckForDebugcon();
    bool CheckForCom1(bool debuggerOnly);

    void ArchPrimeThread(ArchThreadContext** store, uintptr_t stub, uintptr_t entry, uintptr_t arg, uintptr_t stack)
    {
        SwitchFrame frame {};
        sl::MemSet(&frame, 0, sizeof(frame));
        frame.flags = 0x202;
        frame.rdi = reinterpret_cast<uint64_t>(arg);
        frame.rsi = reinterpret_cast<uint64_t>(entry);

        constexpr size_t NullLength = 16;

        sl::MemSet(reinterpret_cast<void*>(stack - NullLength), 0, NullLength);
        stack -= NullLength;
        sl::MemCopy(reinterpret_cast<void*>(stack - sizeof(stub)), &stub, sizeof(stub));
        stack -= sizeof(stub);

        auto* dest = reinterpret_cast<SwitchFrame*>(sl::AlignDown(stack, alignof(SwitchFrame)));
        dest--;
        sl::MemCopy(dest, &frame, sizeof(frame));

        *store = reinterpret_cast<ArchThreadContext*>(dest);
    }

    void ArchInitEarly()
    {
        const uint64_t dummyLocals = reinterpret_cast<uint64_t>(KERNEL_CPULOCALS_BEGIN);
        SetMyLocals(dummyLocals, 0);

        const bool e9Active = CheckForDebugcon();
        CheckForCom1(e9Active); //if debugcon is active, com1 is only allowed to be used for the debugger
        CommonCpuSetup();
    }

    void ArchInitDomain0(InitState& state)
    { (void)state; } //no-op

    void ArchInitFull(uintptr_t& virtBase)
    {
        NPK_ASSERT(InitBspLapic(virtBase));
    }

    KernelMap ArchSetKernelMap(sl::Opt<KernelMap> next)
    {
        const KernelMap prev = READ_CR(3);

        const Paddr future = next.HasValue() ? *next : kernelMap;
        WRITE_CR(3, future);

        return prev;
    }

    bool ArchInitDebugState()
    {
        uint64_t dr7 = READ_DR(7);
        if ((dr7 & (1 << 13)) != 0)
            return false; //we need dr7.GD to be clear to continue

        dr7 &= ~0xFF; //disable local and global matching of hw breakpoints.
        dr7 |= (1 << 8) | (1 << 9); //enable exact matches for breakpoints.
        dr7 &= (1 << 11); //disable RTM, we dont support TSX
        WRITE_DR(7, dr7);

        uint64_t dr6 = READ_DR(6);
        dr6 |= 1 << 11; //cleared by the cpu when a bus lock is asserted,
                        //software is expected to reset this bit.
        dr6 &= ~((1 << 13) | (1 << 14)); //set BD and BS bits
        WRITE_DR(6, dr6);

        return true;
    }

    constexpr uint8_t BindSwBreakpoint = 0xFF;
    uint8_t drBitmap = 0;

    inline void WriteDr(uint8_t index, uint64_t value)
    {
        switch (index)
        {
        case 0: WRITE_DR(0, value); return;
        case 1: WRITE_DR(1, value); return;
        case 2: WRITE_DR(2, value); return;
        case 3: WRITE_DR(3, value); return;
        }
    }

    bool ArchEnableBreakpoint(ArchBreakpoint& bp, uintptr_t addr, size_t kind, bool read, bool write, bool exec, bool hardware)
    {
        if (exec && !hardware)
        {} //TODO: software exec breakpoint

        //find a free debug register
        bp.bind = BindSwBreakpoint;
        for (size_t i = 0; i < 4; i++)
        {
            if (drBitmap & (1 << i))
                continue;

            bp.bind = i;
            drBitmap |= 1 << i;
            break;
        }

        if (bp.bind == BindSwBreakpoint)
            return false;

        //ensure the breakpoint is disabled before we start editing it.
        uint64_t dr7 = READ_DR(7);
        dr7 &= ~(0b11 << (bp.bind * 2));
        WRITE_DR(7, dr7);

        WriteDr(bp.bind, addr);

        //set the break type ...
        dr7 &= ~(0b11 << (bp.bind * 4 + 16));
        if (exec)
            dr7 |= 0b00 << (bp.bind * 4 + 16);
        else if (read)
            dr7 |= 0b11 << (bp.bind * 4 + 16);
        else
            dr7 |= 0b01 << (bp.bind * 4 + 16);
        
        //... and length, if applicable
        dr7 &= ~(0b11 << (bp.bind * 4 + 18));
        if (exec)
            dr7 |= 0b00 << (bp.bind * 4 + 18);
        else if (kind == 1)
            dr7 |= 0b00 << (bp.bind * 4 + 18);
        else if (kind == 2)
            dr7 |= 0b01 << (bp.bind * 4 + 18);
        else if (kind == 4)
            dr7 |= 0b11 << (bp.bind * 4 + 18);
        else if (kind == 8)
            dr7 |= 0b10 << (bp.bind * 4 + 18);
        //TODO: it is technically possible for 8-byte lengths to be
        //unsupported, we should check cpuid for family >=15

        //enable the breakpoint globally
        dr7 |= (0b10 << (bp.bind * 2));
        WRITE_DR(7, dr7);

        return true;
    }

    bool ArchDisableBreakpoint(ArchBreakpoint& bp, uintptr_t addr, size_t kind)
    {
        (void)addr;
        (void)kind;

        if (bp.bind == BindSwBreakpoint)
            return false; //TODO: swbreakpoint support

        uint64_t dr7 = READ_DR(7);
        dr7 &= ~(0b11 << (bp.bind * 2));
        WRITE_DR(7, dr7);

        WriteDr(bp.bind, 0);

        drBitmap &= ~(1 << bp.bind);

        return true;
    }
}
