.global SpinupBlob
.global _EndOfSpinupBlob

.pushsection .rodata

.code16
SpinupBlob:
    cli
    cld

    movl %cs:(BootInfo + 24 - SpinupBlob), %eax
    mov %eax, %cr3

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

    movq BootInfo + 16 - SpinupBlob(%ebx), %rsp
    xor %ebp, %ebp
    movq BootInfo - SpinupBlob(%ebx), %rdi
    jmp *BootInfo + 8 - SpinupBlob(%ebx)

Gdt:
    .8byte 0
    .8byte 0x00AF9B000000FFFF
    .8byte 0x00AF93000000FFFF
Gdtr:
    .2byte 23
    .8byte 0 ## patched at runtime

## These values are patched before each core runs through this code
.align 16
BootInfo:
    .8byte 0 ## software-assigned cpu id
    .8byte 0 ## entry function
    .8byte 0 ## entry stack
    .4byte 0 ## cr3 address
_EndOfSpinupBlob:

.popsection
