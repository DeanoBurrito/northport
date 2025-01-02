.global SwitchFrame

.type SwitchFrame,@function
.size SwitchFrame,(_EndOfSwitchFrame - SwitchFrame)

## void SwitchFrame(TrapFrame** current, void (*callback)(TrapFrame* next, void*), TrapFrame* next, void* callbackArg)
SwitchFrame:
    test %rdx, %rdx ## if `next` is nullptr, return to caller - wtf are they doing
    jnz 1f
    ret
1:
    test %rdi, %rdi ## if `current` is nullptr, no point in saving current gpr since they'll be discarded
    jz 1f

    pop %rax
    mov %rsp, %r9

    ## setup iret frame
    push $0x10
    push %r9
    pushf
    push $0x8
    push %rax

    sub $0x10, %rsp ## error code + vector, we dont care what the values are, only that there is space

    sub $0x8, %rsp ## we dont need to push caller-saved regs
    push %rbx
    sub $0x20, %rsp
    push %rbp
    sub $0x20, %rsp
    push %r12
    push %r13
    push %r14
    push %r15

    mov %rsp, (%rdi)
1:
    ## load next stack
    mov %rdx, %rsp

    ## if callback is non-null, run it
    test %rsi, %rsi
    jz 1f

    mov %rdx, %rdi
    xchg %rcx, %rsi
    mov %rcx, %rsi
    cld
    call *%rcx
1:
    ## load next context, starting with segment regs
    mov 0xA8(%rsp), %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss

    cmp $0x10, %ax
    je 1f
    swapgs
1:
    ## and then GPRs
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
