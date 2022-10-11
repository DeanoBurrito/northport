OUTPUT_FORMAT(elf64-littleriscv)

ENTRY(SbiEntry)

PHDRS
{
    /* boot[text|data] segments are only available during early init. */
    boottext    PT_LOAD     FLAGS((1 << 0) | (1 << 2));
    bootdata    PT_LOAD     FLAGS((1 << 1) | (1 << 2));

    /* Regular kernel segments */
    text        PT_LOAD     FLAGS((1 << 0) | (1 << 2));
    rodata      PT_LOAD     FLAGS((1 << 2));
    data        PT_LOAD     FLAGS((1 << 1) | (1 << 2));
}

SECTIONS
{
    PHYS_BASE = 0x80000000 + 2M;
    VIRT_BASE = 0xFFFFFFFF80000000 + 2M;

    VIRT_OFFSET = VIRT_BASE - PHYS_BASE;
    . = PHYS_BASE;

    KERNEL_BOOT_BEGIN = .;
    .boottext :
    {
        *(.boottext)
    } :boottext

    . += CONSTANT(MAXPAGESIZE);

    .bootdata : 
    {
        *(.bootdata)
    } :bootdata
    KERNEL_BOOT_SIZE = . - KERNEL_BOOT_BEGIN;
    
    . += VIRT_OFFSET;
    KERNEL_BLOB_BEGIN = .;

    KERNEL_TEXT_BEGIN = .;
    .text : AT(ADDR(.text) - VIRT_OFFSET)
    {
        *(.text)
        *(.text.*)
    } :text
    KERNEL_TEXT_SIZE = . - KERNEL_TEXT_BEGIN;

    . += CONSTANT(MAXPAGESIZE);

    KERNEL_RODATA_BEGIN = .;
    .rodata : AT(ADDR(.rodata) - VIRT_OFFSET)
    {
        *(.rodata)
        *(.rodata.*)
        *(.srodata)
        *(.srodata.*)
    } :rodata

    .limine_reqs : AT(ADDR(.limine_reqs) - VIRT_OFFSET)
    {
        KEEP(*(.limine_reqs))
    } :rodata
    KERNEL_RODATA_SIZE = . - KERNEL_RODATA_BEGIN;

    . += CONSTANT(MAXPAGESIZE);

    KERNEL_DATA_BEGIN = .;
    .data : AT(ADDR(.data) - VIRT_OFFSET)
    {
        /* Normally this is where you'd place __global_pointer$. I've chosen not to use linker relaxation in the kernel. */
        /* It complicates trap entry/exists, having to reload gp everytime. It's unnecessary, and frankly the size and */
        /* and speed benefits aren't worth it. */
        
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
    } :data

    .bss : AT(ADDR(.bss) - VIRT_OFFSET)
    {
        *(COMMON)
        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
    } :data
    KERNEL_DATA_SIZE = . - KERNEL_DATA_BEGIN;

    KERNEL_BLOB_SIZE = . - KERNEL_BLOB_BEGIN;
}
