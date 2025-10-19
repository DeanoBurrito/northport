.global HwSwitchThread

.type HwSwitchThread, @function
.size HwSwitchThread, (_EndOfHwSwitchThread - HwSwitchThread)

.pushsection .text
HwSwitchThread:
    test %rdi, %rdi
    jz 1f

    pushfq
    push %r15
    push %r14
    push %r13
    push %r12
    push %rbp
    push %rbx
    push %rsi
    push %rdi

    mov %rsp, (%rdi)
1:
    mov %rsi, %rsp
    pop %rdi
    pop %rsi
    pop %rbx
    pop %rbp
    pop %r12
    pop %r13
    pop %r14
    pop %r15
    popfq
    ret
_EndOfHwSwitchThread:
.popsection
