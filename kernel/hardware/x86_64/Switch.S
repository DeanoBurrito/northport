.global ArchSwitchThread

.type ArchSwitchThread, @function
.size ArchSwitchThread, (_EndOfArchSwitchThread - ArchSwitchThread)

.pushsection .text
ArchSwitchThread:
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
_EndOfArchSwitchThread:
.popsection
