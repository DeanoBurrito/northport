OUTPUT_FORMAT(elf32-m68k)

PHDRS
{
    text        PT_LOAD     FLAGS((1 << 0) | (1 << 2));
    rodata      PT_LOAD     FLAGS((1 << 2));
    data        PT_LOAD     FLAGS((1 << 1) | (1 << 2));
    npkmodule   0x6E706B6D  FLAGS((1 << 1) | (1 << 2));
    dynamic     PT_DYNAMIC  FLAGS((1 << 1) | (1 << 2));
}

SECTIONS
{
    .text :
    {
        *(.text)
        *(.text.*)
    } :text

    . += CONSTANT(MAXPAGESIZE);

    .rodata : 
    {
        *(.rodata)
        *(.rodata.*)
    } :rodata

    . += CONSTANT(MAXPAGESIZE);

    .npkmodule :
    {
        *(.npkmodule)
        *(.npkmodule.*)
    } :data :npkmodule

    .data : 
    {
        *(.data)
        *(.data.*)
    } :data

    .dynamic :
    {
        *(.dynamic)
    } :data :dynamic

    .bss :
    {
        *(COMMON)
        *(.bss)
    } :data

    /DISCARD/ :
    {
        *(.note)
        *(.note.*)
    }
}

