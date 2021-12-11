#include <arch/x86_64/Gdt.h>

namespace Kernel
{
    //pre-calculated values courtesy of pitust from osdev discord.
    [[gnu::aligned(0x8)]]
    uint64_t defaultGdt[] = 
    {
        (uint64_t)0,            //0x00: null

        0x00AF'9B00'0000'FFFF,  //0x08: kernel code
        0x00AF'9300'0000'FFFF,  //0x10: kernel data

        0x00AF'FB00'0000'FFFF,  //0x18: user code
        0x00AF'F300'0000'FFFF,  //0x20: user data
    };

    void TssEntry::SetIST(size_t index, uint64_t value)
    {
        values[index * 2 + 9] = value & 0xFFFF'FFFF;
        values[index * 2 + 10] = value >> 32;
    }

    uint64_t TssEntry::GetIst(size_t index) const
    {
        uint64_t value = 0;
        value |= values[index * 2 + 9];
        value |= (uint64_t)values[index * 2 + 10] << 32;
        return value;
    }

    void TssEntry::SetRSP(size_t privilegeLevel, uint64_t value)
    {
        values[privilegeLevel * 2 + 1] = value & 0xFFFF'FFFF;
        values[privilegeLevel * 2 + 2] = value >> 32;
    }

    uint64_t TssEntry::GetRSP(size_t privilegeLevel) const
    {
        uint64_t value = 0;
        value |= values[privilegeLevel * 2 + 1];
        value |= (uint64_t)values[privilegeLevel * 2 + 2] << 32;
        return value;
    }

    [[gnu::aligned(0x8)]]
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
            lretq \n\
        ");
    }
}