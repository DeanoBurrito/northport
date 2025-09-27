#include <hardware/Arch.hpp>

namespace Npk
{
    constexpr uint8_t Int3Opcode = 0xCC;

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
