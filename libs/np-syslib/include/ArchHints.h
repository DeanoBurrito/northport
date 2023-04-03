#pragma once

namespace sl
{
#if defined(__riscv)
    [[gnu::always_inline]]
    inline void HintSpinloop()
    {
        /*  So, all the brilliant minds behind riscv decided not to include a 'pause'
            hint in the base ISA, so it's an extension. This means not all compilers support
            it, and even though it's a type of fence instruction, its actually an
            illegal encoding according to the base ISA (lol), so it can't be written like
            that.
            The only remaining option is to pre-calculate the bits of the instruction,
            and assemble it like this.
        */
        asm volatile(".int 0x0100000F");
    }
#elif defined(__x86_64__)
    [[gnu::always_inline]]
    inline void HintSpinloop()
    {
        asm volatile("pause");
    }
#else
    #warning "Compiling np-syslib for unknown target platform. Architecture hints aren't available."

    inline void HintSpinloop()
    {} //no-op
#endif
}
