#include <hardware/x86_64/Private.hpp>
#include <hardware/x86_64/Msr.hpp>
#include <Core.hpp>
#include <private/Debugger.hpp>
#include <lib/Memory.hpp>

//TODO: too many magic numbers!
namespace Npk
{
    constexpr uint8_t BindSwBreakpoint = 0xFF;
    constexpr uint64_t EFlagsTrapFlag = 1 << 8;
    constexpr uint64_t Dr6Breakpoint0 = 1 << 0;
    constexpr uint64_t Dr6Breakpoint1 = 1 << 1;
    constexpr uint64_t Dr6Breakpoint2 = 1 << 2;
    constexpr uint64_t Dr6Breakpoint3 = 1 << 3;
    constexpr uint64_t Dr6BusLock = 1 << 11;
    constexpr uint64_t Dr6DebugRegAccess = 1 << 13;
    constexpr uint64_t Dr6SingleStep = 1 << 14;

    static uint8_t drBitmap = 0;

    void HandleDebugException(TrapFrame* frame, bool int3)
    {
        using namespace Private;

        BreakpointEventArg arg {};
        arg.frame = frame;

        if (int3)
        {
            arg.addr = frame->iret.rip - 1;
            DebugEventOccurred(DebugEventType::Breakpoint, &arg);
            return;
        }

        const uint64_t dr6 = READ_DR(6);
        NPK_ASSERT(!(dr6 & Dr6DebugRegAccess));

        if ((dr6 & Dr6SingleStep) != 0)
        {
            arg.type = BreakpointType::SingleStep;
            DebugEventOccurred(DebugEventType::Breakpoint, &arg);
        }

        if ((dr6 & Dr6Breakpoint0) != 0)
            arg.addr = READ_DR(0);
        else if ((dr6 & Dr6Breakpoint1) != 0)
            arg.addr = READ_DR(1);
        else if ((dr6 & Dr6Breakpoint2) != 0)
            arg.addr = READ_DR(2);
        else if ((dr6 & Dr6Breakpoint3) != 0)
            arg.addr = READ_DR(3);
        else
            return;

        arg.type = BreakpointType::Breakpoint;
        DebugEventOccurred(DebugEventType::Breakpoint, &arg);
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
        if (index > 3)
            return;

        switch (index)
        {
        case 0:
            WRITE_DR(0, value);
            return;

        case 1:
            WRITE_DR(1, value);
            return;

        case 2:
            WRITE_DR(2, value);
            return;

        case 3:
            WRITE_DR(3, value);
            return;

        }
        WRITE_DR(index, value);
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

    size_t HwGetBreakpointCount()
    {
        return 4;
    }

    NpkStatus HwAccessRegister(TrapFrame& frame, HwReg reg, size_t* usedLen,
        sl::Span<uint8_t> buffer, bool write)
    {
        uint64_t store;
        void* source = nullptr;
        bool readonly = false;

        switch (reg)
        {
        case HwReg::ProgramCounter:
            source = &frame.iret.rip;
            break;
        case HwReg::Flags:
            store = frame.iret.flags & 0xFFFF'FFFF;
            source = &store;
            break;
        case HwReg_rax:
            source = &frame.rax;
            break;
        case HwReg_rbx:
            source = &frame.rbx;
            break;
        case HwReg_rcx:
            source = &frame.rcx;
            break;
        case HwReg_rdx:
            source = &frame.rdx;
            break;
        case HwReg_rdi:
            source = &frame.rdi;
            break;
        case HwReg_rsi:
            source = &frame.rsi;
            break;
        case HwReg::FramePointer:
        case HwReg_rbp:
            source = &frame.rbp;
            break;
        case HwReg::StackPointer:
        case HwReg_rsp:
            source = &frame.iret.rsp;
            break;
        case HwReg_r8:
            source = &frame.r8;
            break;
        case HwReg_r9:
            source = &frame.r9;
            break;
        case HwReg_r10:
            source = &frame.r10;
            break;
        case HwReg_r11:
            source = &frame.r11;
            break;
        case HwReg_r12:
            source = &frame.r12;
            break;
        case HwReg_r13:
            source = &frame.r13;
            break;
        case HwReg_r14:
            source = &frame.r14;
            break;
        case HwReg_r15:
            source = &frame.r15;
            break;

        case HwReg_cs:
            source = &frame.iret.cs;
            break;
        case HwReg_ss:
            source = &frame.iret.ss;
            break;
        case HwReg_ds:
            store = READ_SR(ds);
            source = &store;
            readonly = true;
            break;
        case HwReg_es:
            store = READ_SR(es);
            source = &store;
            readonly = true;
            break;
        case HwReg_fs:
            store = READ_SR(fs);
            source = &store;
            readonly = true;
            break;
        case HwReg_gs:
            store = READ_SR(gs);
            source = &store;
            readonly = true;
            break;
        default:
            return NpkStatus::InvalidArg;
        }

        if (source == nullptr)
            return NpkStatus::InvalidArg;
        if (readonly && write)
            return NpkStatus::NotWritable;
        if (buffer.Empty())
            return NpkStatus::InvalidArg;

        const size_t bytes = HwGetRegisterWidth(reg);
        if (bytes == 0 || bytes > buffer.Size())
            return NpkStatus::InvalidArg;

        if (write)
            sl::MemCopy(source, buffer.Begin(), bytes);
        else
            sl::MemCopy(buffer.Begin(), source, bytes);

        if (usedLen != nullptr)
            *usedLen = bytes;

        return NpkStatus::Success;
    }

    size_t HwGetRegisterWidth(HwReg reg)
    {
        switch (reg)
        {
        case HwReg::ProgramCounter:
        case HwReg::StackPointer:
        case HwReg::FramePointer:
            return 8;
        case HwReg::Flags:
            return 4;

        case HwReg_rax:
        case HwReg_rbx:
        case HwReg_rcx:
        case HwReg_rdx:
        case HwReg_rdi:
        case HwReg_rsi:
        case HwReg_rbp:
        case HwReg_rsp:
        case HwReg_r8:
        case HwReg_r9:
        case HwReg_r10:
        case HwReg_r11:
        case HwReg_r12:
        case HwReg_r13:
        case HwReg_r14:
        case HwReg_r15:
            return 8;

        case HwReg_cs:
        case HwReg_ss:
        case HwReg_ds:
        case HwReg_es:
        case HwReg_fs:
        case HwReg_gs:
            return 4;

        default:
            return 0;
        }
    }

    size_t HwGetRegisterCount(HwRegType type)
    {
        switch (type)
        {
        case HwRegType::Common:
            return 4;

        case HwRegType::General:
            return 16;

        case HwRegType::FloatingPoint:
            return 0;

        case HwRegType::Vector:
            return 0;

        case HwRegType::System:
            return 6;

        default:
            return 0;
        }
    }

    void HwSetSingleStep(TrapFrame& frame, bool on)
    {
        frame.iret.flags &= ~EFlagsTrapFlag;
        if (on)
            frame.iret.flags |= EFlagsTrapFlag;
    }

    bool HwGetSingleStep(TrapFrame& frame)
    {
        return frame.iret.flags & EFlagsTrapFlag;
    }
}
