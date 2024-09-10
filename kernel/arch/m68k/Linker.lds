OUTPUT_FORMAT(elf32-m68k)

ENTRY(KernelEntry)

PHDRS
{
    text    PT_LOAD     FLAGS((1 << 0) | (1 << 2));
    rodata  PT_LOAD     FLAGS((1 << 2));
    data    PT_LOAD     FLAGS((1 << 1) | (1 << 2));
    dynamic PT_DYNAMIC  FLAGS((1 << 1) | (1 << 2));
}

SECTIONS
{
    . = 0xFF900000;
    KERNEL_BLOB_BEGIN = .;

    KERNEL_TEXT_BEGIN = .;
    .text :
    {
        *(.text)
        *(.text.*)
    } :text
    KERNEL_TEXT_END = .;

    . += CONSTANT(MAXPAGESIZE);

    KERNEL_RODATA_BEGIN = .;
    .rodata : 
    {
        *(.rodata)
        *(.rodata.*)
    } :rodata

    .limine_reqs :
    {
        KEEP(*(.limine_reqs))
    } :rodata
    KERNEL_RODATA_END = .;

    . += CONSTANT(MAXPAGESIZE);

    KERNEL_DATA_BEGIN = .;
    .got : { 
        *(.got.plt) 
        *(.got) 
    } :data

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
        *(.interp)
        *(.comment)
    }
    KERNEL_DATA_END = .;

    KERNEL_BLOB_END = .;
}
