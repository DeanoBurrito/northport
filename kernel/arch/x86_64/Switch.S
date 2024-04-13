.global SwitchFrame

.type SwitchFrame,@function
.size SwitchFrame,(_EndOfSwitchFrame - SwitchFrame)

# void SwitchFrame(TrapFrame** current, TrapFrame* next)
SwitchFrame:
    test %rsi, %rsi
    jnz 1f
    ret # next frame is null? just return
1:
    test %rdi, %rdi
    jz SkipSave # if current frame is null, dont save to it

    pop %rax # backup RIP/where this function will return to.
    mov %rsp, %rcx # stack pointer for returned-to function.

    push $0x10 # setup iret frame (ss:rsp, flags, cs:rip)
    push %rcx
    pushf
    push $0x8
    push %rax

    sub $0x10, %rsp # EC + vector

    push $0 # we only need to save the callee saved registers,
    push %rbx # the rest are saved before calling this function.
    push $0
    push $0
    push $0
    push $0
    push %rbp
    push $0
    push $0
    push $0
    push $0
    push %r12
    push %r13
    push %r14
    push %r15

    mov %rsp, (%rdi)
SkipSave:
    mov %rsi, %rsp

    # load data segments + gs.base if needed
    mov 0xA8(%rsp), %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs

    cmp $0x10, %ax
    je 1f
    swapgs
1:
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax

    add $0x10, %rsp
    iretq
_EndOfSwitchFrame:
