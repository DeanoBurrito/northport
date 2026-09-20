.global ApSpinupBlob
.global ApSpinupBlobData
.global _EndOfApSpinupBlob

.set Table_NextCpuId, 0
.set Table_EntryCount, 4
.set Table_LateCpus, 8
.set Table_EntryStride, 24
.set Table_Entries, 32
.set Entry_Entry, 8
.set Entry_StackBase, 16
.set Entry_StackLen, 24

.pushsection .data
.code16
ApSpinupBlob:
    cli
    cld

    ## Pre-load cr3 with the kernel page table root
    movl %cs:(ApSpinupCr3 - ApSpinupBlob), %eax
    mov %eax, %cr3

    ## Prepare cr4, efer and cr0 for long mode. All that remains if to enable
    ## paging when we're ready to commit to the jump.
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

    ## Get the absolute address of the spinup blob into ebx
    mov %cs, %ebx
    shl $4, %ebx

    ## Load gdt, finish entering long mode
    lea Gdt - ApSpinupBlob(%ebx), %eax
    mov %eax, Gdtr - ApSpinupBlob + 2(%ebx)

    lgdt %cs:(Gdtr - ApSpinupBlob)

    lea LongMode - ApSpinupBlob(%ebx), %eax
    mov %eax, FarJmp - ApSpinupBlob(%ebx)

    ljmpl *%cs:(FarJmp - ApSpinupBlob)
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

    mov WakeTable - ApSpinupBlob(%ebx), %rsi

    ## Claim a software id for this cpu
    mov $1, %eax
    lock xadd %eax, Table_NextCpuId(%rsi)

    ## Determine is this cpu claimed too late
    cmp Table_EntryCount(%rsi), %eax
    jae LateClaim

    ## Calculate and place the address of the claimed wait entry into rdi,
    ## rdi is left alone until jumping to higher level code so this also
    ## counts are loading the argument for the entry function.
    imul Table_EntryStride(%rsi), %rax
    lea Table_Entries(%rsi), %rdi
    add %rax, %rdi

    ## Load stack and align it how the sys-v abi expects
    mov Entry_StackBase(%rdi), %rsp
    add Entry_StackLen(%rdi), %rsp
    pushq $0
    xor %ebp, %ebp

    mov Entry_Entry(%rdi), %rax
    jmp *%rax
    ud2

LateClaim:
    ## Cpu tried to claim an id too late, too bad! Increment the late claim
    ## counter so the BSP can observe this and then we just spin here forever.
    lock incl Table_LateCpus(%rsi)
    cli
1:
    hlt
    jmp 1b

Gdt:
    .8byte 0
    .8byte 0x00AF9B000000FFFF
    .8byte 0x00AF93000000FFFF
Gdtr:
    .2byte 23
    .8byte 0 ## patched at runtime

## These values are patched by higher level code at runtime to contain the
## kernel's page table root address and virtual address of the wake structure.
.align 8
ApSpinupBlobData:
ApSpinupCr3:
.8byte 0
WakeTable:
.8byte 0
_EndOfApSpinupBlob:

.code64
.popsection
