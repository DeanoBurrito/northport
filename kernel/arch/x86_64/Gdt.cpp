#include <arch/x86_64/Gdt.h>
#include <arch/x86_64/Tss.h>
#include <Platform.h>

namespace Kernel
{
    [[gnu::aligned(8)]]
    uint64_t gdtEntries[7] = 
    {
        0,                      //0x00: null selector
        0x00AF'9B00'0000'FFFF,  //0x08: kernel code
        0x00AF'9300'0000'FFFF,  //0x10: kernel data
        0x00AF'FB00'0000'FFFF,  //0x18: user code
        0x00AF'F300'0000'FFFF,  //0x20: user data
        0,                      //0x28: tss low
        0,                      //0x30: tss high
    };

    const Gdtr defaultGdtr = 
    { 
        .limit = 7 * sizeof(uint64_t),
        .address = (size_t)gdtEntries
    };

    void SetTssDescriptorAddr(uint64_t newAddr)
    {
        const uint32_t descType = 0b1001;

        uint64_t low = (uint16_t)(sizeof(TaskStateSegment) - 1) | newAddr << 16;
        uint64_t med = (uint8_t)(newAddr >> 16) | (newAddr & 0xFF00'0000) | (descType << 8) | (1 << 15) | (1 << 20); //bit15 = present, bit20 = avail
        uint64_t high = newAddr >> 32; 

        gdtEntries[5] = low | (med << 32);
        gdtEntries[6] = high;
    }

    [[gnu::naked]]
    void FlushGDT()
    {   
        //load the new gdt
        asm volatile("lgdt %0" :: "m"(defaultGdtr));

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
            lretq \n\
        ");
    }
}