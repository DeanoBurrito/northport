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
    KERNEL_BLOB_BEGIN = .;

    KERNEL_TEXT_BEGIN = .;
    .text :
    {
        *(.text)
        *(.text.*)
    } :text
    KERNEL_TEXT_SIZE = . - KERNEL_TEXT_BEGIN;

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
    KERNEL_RODATA_SIZE = . - KERNEL_RODATA_BEGIN;

    . += CONSTANT(MAXPAGESIZE);

    KERNEL_DATA_BEGIN = .;
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
    KERNEL_DATA_SIZE = . - KERNEL_DATA_BEGIN;

    KERNEL_BLOB_SIZE = . - KERNEL_BLOB_BEGIN;
}
