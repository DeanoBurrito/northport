.global SpinupBlob
.global SpinupBootInfo
.global _EndOfSpinupBlob

.pushsection .rodata

.code16
SpinupBlob:
    cli
    cld

    movl %cs:(BootInfo_Cr3 - SpinupBlob), %eax
    mov %eax, %cr3

    ## Prepare cr4, efer and cr0 for long mode
    mov %cr4, %eax
    or $(1 << 5), %eax
    mov %eax, %cr4

    mov $0xC0000080, %ecx
    rdmsr
    or $((1 << 8) | (1 << 11)), %eax
    wrmsr

    mov %cr0, %eax
    or $((1 << 31) | (1 << 0)), %eax
    mov %eax, %cr0

    ## Setup PAT to mirror BSP (copying the limine layout)
    mov $1, %eax
    xor %ecx, %ecx
    cpuid
    test $(1 << 16), %edx
    jz 1f
    mov $0x00070406, %eax
    mov $0x00000105, %edx
    mov $0x277, %ecx
    wrmsr
1:

    ## Load gdt, finish entering lmode
    mov %cs, %ebx
    shl $4, %ebx
    lea Gdt - SpinupBlob(%ebx), %eax
    mov %eax, Gdtr - SpinupBlob + 2(%ebx)

    lgdt %cs:(Gdtr - SpinupBlob)

    lea LongMode - SpinupBlob(%ebx), %eax
    mov %eax, FarJmp - SpinupBlob(%ebx)

    ljmpl *%cs:(FarJmp - SpinupBlob)
FarJmp:
    .4byte 0
    .2byte 0x8

.align 8
.code64
LongMode:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %ss
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    movq BootInfo_Stack - SpinupBlob(%ebx), %rsp
    xor %ebp, %ebp
    movq BootInfo_Arg - SpinupBlob(%ebx), %rdi
    mov BootInfo_Entry - SpinupBlob(%ebx), %rcx

    ## All the boot info data is now in registers, tell the bsp
    ## we booted successfully.
    movq $1, BootInfo_BootedFlag - SpinupBlob(%ebx)

    jmp *%rcx
    ud2

Gdt:
    .8byte 0
    .8byte 0x00AF9B000000FFFF
    .8byte 0x00AF93000000FFFF
Gdtr:
    .2byte 23
    .8byte 0 ## patched at runtime

## These values are patched before each core runs through this code
.align 8
BootInfo:
    BootInfo_Arg:
    .8byte 0 ## cpu-local storage, contains software assigned id
    BootInfo_Entry:
    .8byte 0 ## entry function
    BootInfo_Stack:
    .8byte 0 ## entry stack
    BootInfo_BootedFlag:
    .8byte 0 ## flag to indicate to bsp that we're alive
    BootInfo_Cr3:
    .8byte 0 ## cr3 address
_EndOfSpinupBlob:

.code64
.popsection
