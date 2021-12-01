OUTPUT_FORMAT(elf64-x86-64)

ENTRY(_KernelEntry)

PHDRS
{
    text    PT_LOAD     FLAGS((1 << 0) | (1 << 2));
    rodata  PT_LOAD     FLAGS((1 << 2));
    data    PT_LOAD     FLAGS((1 << 1) | (1 << 2));
}

SECTIONS
{
    . = 0xffffffff80000000;
    
    .text :
    {
        *(.text*)
    } :text

    . += CONSTANT(MAXPAGESIZE);

    /* Lets us boot straight out of limine if we want, unused otherwise */
    .stivale2hdr :
    {
        KEEP(*(.stivale2hdr))
    } :rodata

    .rodata : 
    {
        *(.rodata*)
    } :rodata

    . += CONSTANT(MAXPAGESIZE);

    .data : 
    {
        *(.data*)
    } :data

    .bss :
    {
        *(COMMON)
        *(.bss*)
    } :data
}
