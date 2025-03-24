OUTPUT_FORMAT(elf64-x86-64)

ENTRY(RealEntry)

PHDRS
{
    text    PT_LOAD     FLAGS((1 << 0) | (1 << 2));
    rodata  PT_LOAD     FLAGS((1 << 2));
    data    PT_LOAD     FLAGS((1 << 1) | (1 << 2));
    dynamic PT_DYNAMIC  FLAGS((1 << 1) | (1 << 2));
}

SECTIONS
{
    . = 0xffffffff80000000;
    KERNEL_BLOB_BEGIN = .;

    KERNEL_TEXT_BEGIN = .;
    .text :
    {
        *(.text)
        *(.text.*)
        *(.ltext)
        *(.ltext.*)
    } :text
    KERNEL_TEXT_END = .;

    . += CONSTANT(MAXPAGESIZE);

    KERNEL_RODATA_BEGIN = .;
    .rodata : 
    {
        *(.rodata)
        *(.rodata.*)
        *(.lrodata)
        *(.lrodata.*)
    } :rodata

    .init_array :
    {
        INIT_ARRAY_BEGIN = .;
        KEEP(*(.init_array .init_array.*))
        KEEP(*(.ctors .ctors.*))
        INIT_ARRAY_END = .;
    } :rodata

    .limine_reqs :
    {
        KEEP(*(.limine_reqs))
    } :rodata
    KERNEL_RODATA_END = .;

    . += CONSTANT(MAXPAGESIZE);

    KERNEL_DATA_BEGIN = .;
    .data : 
    {
        *(.data)
        *(.data.*)
        *(.ldata)
        *(.ldata.*)
    } :data

    .dynamic :
    {
        *(.dynamic)
    } :data :dynamic

    .bss :
    {
        *(COMMON)
        *(.bss .bss.*)
        *(.lbss .lbss.*)
    } :data

    /DISCARD/ :
    {
        *(.note .note.*)
        *(.comment)
        *(.interp)
    }
    KERNEL_DATA_END = .;

    KERNEL_BLOB_END = .;
}
