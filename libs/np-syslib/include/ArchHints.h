#pragma once

#include <Compiler.h>

namespace sl
{
#if defined(__riscv)
    SL_ALWAYS_INLINE
    void HintSpinloop()
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

    //TODO: Dma[Write|Read]Barrer()

#elif defined(__x86_64__)
    SL_ALWAYS_INLINE
    void HintSpinloop()
    {
        asm("pause");
    }

    SL_ALWAYS_INLINE
    void DmaWriteBarrier()
    { 
        asm("sfence"); 
    }

    SL_ALWAYS_INLINE
    void DmaReadBarrier()
    { 
        asm("lfence"); 
    }

#elif defined(__m68k__)
    SL_ALWAYS_INLINE
    void HintSpinloop()
    { } //afaik there are no instructions for this on the 68k series chips, so do nothing

    //TODO: Dma[Write|Read]Barrer()
#else
    #error "Unknown target architecture, cannot provide arch-specific hints"
#endif
}
