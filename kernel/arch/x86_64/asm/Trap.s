# There are a few functions here:
# - TrapEntry, which assumes that an error code (or at least a dummy) and then
# the vector number have been pushed onto the stack.
# - TrapExit, which takes the new stack to switch to in rdi. 
# TrapEntry calls InterruptDispatch, which then returns and falls through to TrapExit.
#
# TrapExit also checks if the top value on the stack is one, and if so it assumes it needs to swap cr3.
# It'll pop two values from the stack in the following order: the new cr3, and the stack to swap to.
# After reloading cr3, itll call TrapExit with the new stack. 

.global TrapEntry
.global TrapExit

.extern InterruptDispatch

.section .text

# NOTE: TrapEntry assumes the error code and vector numbers are already on the stack.
TrapEntry:
    # Ensure SMAP is enforced by clearing AC
    clac

    # Push all the general purpose registers. We push a dummy rsp here, since the iret
    # frame contains the real ss:rsp.
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    pushq $0
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15

    # Check if we need to swapgs or not
    mov %ds, %ax
    cmp $0x10, %ax
    je 1f
    swapgs
1:

    # Load kernel data segments, CS was set when we entered the interrupt handler.
    mov $0x10, %eax
    mov %ax, %ds
    mov %ax, %ss
    mov %ax, %es
    mov %ax, %fs

    # Stack key: local (hopefully haha)
    pushq $0

    # Pass current saved stack via rdi (first x86_64 arg), and call dispatch.
    mov %rsp, %rdi
    call InterruptDispatch

    # Load the returned stack from rax into rsp.
    mov %rax, %rsp

    # Check if we need to modify cr3 before reloading the stack
    cmpq $0x1, (%rsp)
    je SwapToForeignStack
    # ... or just continue on through TrapExit
    jmp 1f

# TrapExit takes the new stack to load in rdi (first arg in x86_64)
TrapExit:
    mov %rdi, %rsp
1:
    add $0x8, %rsp

    # Load the appropriate data segments for the destination ring.
    # This assumes that iret_ss is valid. If its not, boom.
    mov 0xB0(%rsp), %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs

    # Update GS if needed
    cmp $0x10, %ax
    je 1f
    swapgs
1:

    # Pop all general purpose regs. The add is to remove the dummy rsp value from earlier.
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    add $0x8, %rsp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax

    # Remove the vector number and error code, then execute iret
    add $0x10, %rsp
    iretq

SwapToForeignStack:
    # Consume the magic value used to get here.
    add $0x8, %rsp

    # Once we reload cr3 this stack becomes innaccessible, so we store the target stack
    # in %rdi (for TrapExit to use) beforehand.
    pop %rax
    pop %rdi

    mov %rax, %cr3
    jmp TrapExit
