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
    KERNEL_TEXT_BEGIN = .;

    .text :
    {
        *(.text*)
    } :text

    KERNEL_TEXT_END = .;
    . += CONSTANT(MAXPAGESIZE);
    KERNEL_RODATA_BEGIN = .;

    .rodata : 
    {
        *(.rodata*)
    } :rodata

    KERNEL_RODATA_END = .;
    . += CONSTANT(MAXPAGESIZE);
    KERNEL_DATA_BEGIN = .;

    .data : 
    {
        *(.data*)
    } :data

    .bss :
    {
        *(COMMON)
        *(.bss*)
    } :data

    KERNEL_DATA_END = .;
}
