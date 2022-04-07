# This file gets copied to 0x44000 (physical), and config data is placed at 0x45000.
# This memory is reserved early in the kernel boot process and should always be available, 
# even if the bootloader has used traditionally available memory below 1MB.
#
# This trampoline gets the cpu into long-mode using a temporary gdt and page tables,
# before eventually jumping to the provided goto_address using the provided stack.
# This mimics the stivale2 protocol, and should be familiar if you've used that.
# Optionally 5-level paging and execute-disable are enabled based on flags set by the bsp.
#
# Each core has a specific config block, where it gets it's specific stack and destination
# functions from. This is basically just an array at a fixed address (0x45100), with the index into it being the lapic id.

.text
.code16

RelocOffset:
    # ensure CS is set to a known value
    jmp $0x4400, $(Stage1 - RelocOffset)

Stage1:
    cli
    cld

    # setup other segment registers to mirror cs
    mov $0x4400, %ax
    mov %ax, %ds
    mov %ax, %ss
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    # load our all-purpose gdt
    lgdtl (0x1110)

    # setup cr3 for paging
    movl (0x1108), %eax
    mov %eax, %cr3

    # enable la57 if needed, and PAE
    mov $0x20, %eax
    testw $(1 << 1), (0x1118)
    jz 1f
    orl $(1 << 12), %eax
1:
    mov %eax, %cr4

    //set LME and optionally NX bit if requested
    mov $0xC0000080, %ecx
    rdmsr
    orl $(1 << 8), %eax
    testw $(1 << 0), (0x1118)
    jz 1f
    orl $(1 << 11), %eax
1:
    wrmsr

    # enable protected mode and paging (and also caches) jumping straight into long mode
    mov $0x80000011, %eax
    mov %eax, %cr0

    # prepare indirect jump to 64-bit CS, then execute it
    movl $(Stage2 - RelocOffset + 0x44000), (Stage2JumpData - RelocOffset)
    ljmpl *(Stage2JumpData - RelocOffset)

Stage2JumpData:
    .int 0
    .int 0x28

.code64
Stage2:

    # set data selectors to 64-bit versions as well
    mov $0x30, %ax
    mov %ax, %ds
    mov %ax, %ss
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    # we'll need the apic id to access our core's config, store that in %edi.
    mov $0x1B, %ecx
    rdmsr
    and $0xFFFFF000, %eax
    add $0x20, %eax
    mov (%eax), %edi
    shr $24, %edi

    # edi now contais our lapic id
    mov %edi, %eax
    mov $0x20, %ebx
    mul %ebx
    addl $0x45120, %eax
    mov %eax, %esi

    # esi contains the base address of our config block
    movq %rdi, 0(%esi)
    mov %rsi, %rdi
    
Stall:
    # monitor our config block's %rip until we have a valid address
    movq 0x8(%esi), %rax
    test %rax, %rax

    jnz ExitStall
    pause
    jmp Stall

ExitStall:
    # zero data registers
    xor %rax, %rax
    xor %rbx, %rbx
    xor %rcx, %rcx
    xor %rdx, %rdx
    xor %rsi, %rsi
    xor %rbp, %rbp
    xor %r8, %r8
    xor %r9, %r9
    xor %r10, %r10
    xor %r11, %r11
    xor %r12, %r12
    xor %r13, %r13
    xor %r14, %r14
    xor %r15, %r15

    # set stack then jump to target function
    movq 0x10(%rdi), %rsp
    push $0
    push 0x8(%rdi)

    ret


