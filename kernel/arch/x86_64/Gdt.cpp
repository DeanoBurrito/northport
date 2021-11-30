#include <arch/x86_64/Gdt.h>

namespace Kernel
{
    //pre-calculated values courtesy of pitust from osdev discord.
    uint64_t defaultGdt[] = 
    {
        (uint64_t)0,            //0x00: null

        0x00AF'9B00'0000'FFFF,  //0x08: kernel code
        0x00AF'9300'0000'FFFF,  //0x10: kernel data

        0x00AF'FB00'0000'FFFF,  //0x18: user code
        0x00AF'F300'0000'FFFF,  //0x20: user data
    };

    GDTR defaultGDTR;

    void SetupGDT()
    {
        defaultGDTR.address = (uint64_t)defaultGdt;
        defaultGDTR.limit = 5 * sizeof(uint64_t);
    }

    [[gnu::naked]]
    void FlushGDT()
    {   
        //load the new gdt
        asm volatile("lgdt %0" :: "m"(defaultGDTR));

        //reset all selectors (except cs), then reload cs with a far return
        //NOTE: this is a huge hack, as we assume rip was pushed to the stack (call instruction was used to come here).
        asm volatile("\
            mov $0x10, %ax \n\
            mov %ax, %ds \n\
            mov %ax, %es \n\
            mov %ax, %fs \n\
            mov %ax, %gs \n\
            mov %ax, %ss \n\
            pop %rdi \n\
            push $0x8 \n\
            push %rdi \n\
            retfq \n\
        ");
    }
}