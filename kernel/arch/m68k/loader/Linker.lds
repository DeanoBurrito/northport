OUTPUT_FORMAT(elf32-m68k)

ENTRY(LoaderEntry)

PHDRS
{
    text    PT_LOAD     FLAGS((1 << 0) | (1 << 2));
    rodata  PT_LOAD     FLAGS((1 << 2));
    data    PT_LOAD     FLAGS((1 << 1) | (1 << 2));
}

SECTIONS
{
    . = 0x1000;

    LOADER_BLOB_BEGIN = .;
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

    .data : 
    {
        *(.data)
        *(.data.*)
    } :data

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
    LOADER_BLOB_END = .;
}
