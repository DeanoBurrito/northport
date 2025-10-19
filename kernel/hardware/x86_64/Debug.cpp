#include <hardware/x86_64/Private.hpp>
#include <hardware/x86_64/Msr.hpp>
#include <Debugger.hpp>
#include <Memory.hpp>

namespace Npk
{
    constexpr uint8_t BindSwBreakpoint = 0xFF;
    uint8_t drBitmap = 0;

    void HandleDebugException(TrapFrame* frame, bool int3)
    {
        BreakpointEventArg arg {};
        arg.frame = frame;

        if (int3)
        {
            arg.addr = frame->iret.rip - 1;
            DebugEventOccured(DebugEventType::Breakpoint, &arg);
            return;
        }

        const uint64_t dr6 = READ_DR(6);
        if (dr6 & 0b0001)
            arg.addr = READ_DR(0);
        else if (dr6 & 0b0010)
            arg.addr = READ_DR(1);
        else if (dr6 & 0b0100)
            arg.addr = READ_DR(2);
        else if (dr6 & 0b1000)
            arg.addr = READ_DR(3);

        DebugEventOccured(DebugEventType::Breakpoint, &arg);
    }

    bool HwInitDebugState()
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

    bool HwEnableBreakpoint(HwBreakpoint& bp, uintptr_t addr, size_t kind, 
        bool read, bool write, bool exec, bool hardware)
    {
        (void)write;

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

    bool HwDisableBreakpoint(HwBreakpoint& bp, uintptr_t addr, size_t kind)
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

    constexpr size_t GprBase = 0;
    constexpr size_t FprBase = 0x100;
    constexpr size_t VectorBase = 0x200;
    constexpr size_t SpecialBase = 0x300;

    size_t AccessRegister(TrapFrame& frame, size_t index, 
        sl::Span<uint8_t> buffer, bool get)
    {
        void* reg = nullptr;
        size_t regSize = 0;
        bool readonly = false;
        uint64_t stash;

        if (index < GprBase + 16)
        {
            regSize = 8;

            switch (index)
            {
            case 0: reg = &frame.rax; break;
            case 1: reg = &frame.rbx; break;
            case 2: reg = &frame.rcx; break;
            case 3: reg = &frame.rdx; break;
            case 4: reg = &frame.rsi; break;
            case 5: reg = &frame.rdi; break;
            case 6: reg = &frame.iret.rsp; break;
            case 7: reg = &frame.rbp; break;
            case 8: reg = &frame.r8; break;
            case 9: reg = &frame.r9; break;
            case 10: reg = &frame.r10; break;
            case 11: reg = &frame.r11; break;
            case 12: reg = &frame.r12; break;
            case 13: reg = &frame.r13; break;
            case 14: reg = &frame.r14; break;
            case 15: reg = &frame.r15; break;
            }
        }
        //TODO: fpu and vector regs
        else if (index >= SpecialBase && index < SpecialBase + 11)
        {
            regSize = 8;
            index -= SpecialBase;

            switch (index)
            {
            case 0: reg = &frame.iret.rip; break;
            case 1: reg = &frame.iret.flags; break;
            case 2: reg = &frame.iret.cs; break;
            case 3: reg = &frame.iret.ss; break;
            case 4:
                stash = READ_CR(0);
                reg = &stash;
                readonly = true;
                break;
            case 5:
                stash = READ_CR(1);
                reg = &stash;
                readonly = true;
                break;
            case 6:
                stash = READ_CR(2);
                reg = &stash;
                readonly = true;
                break;
            case 7:
                stash = READ_CR(3);
                reg = &stash;
                readonly = true;
                break;
            case 8:
                stash = READ_CR(4);
                reg = &stash;
                readonly = true;
                break;
            case 9:
                stash = READ_CR(8);
                reg = &stash;
                readonly = true;
                break;
            case 10:
                stash = ReadMsr(Msr::Efer);
                reg = &stash;
                readonly = true;
                break;
            //TODO: ds, es, fs, gs
            }
        }

        if (reg == nullptr)
            return 0; //unknown register
        if (readonly && !get)
            return 0; //trying to write a readonly register
        if (buffer.Empty())
            return regSize;
        if (buffer.Size() < regSize)
            return 0; //not enough buffer space for read/write

        if (get)
            sl::MemCopy(buffer.Begin(), reg, regSize);
        else
            sl::MemCopy(reg, buffer.Begin(), regSize);

        return regSize;
    }
}
