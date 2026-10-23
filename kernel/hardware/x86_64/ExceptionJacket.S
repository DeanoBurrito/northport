.global HwExceptionJacket

.type HwExceptionJacket, @function
.size HwExceptionJacket, (_EndOfHwExceptionJacket - HwExceptionJacket)

## using HwJacketFunc = void (*)(void* arg);
## 
## HwExceptionFlags HwExceptionJacket(HwJacketFunc func, void* arg,
##     HwExceptionFlags flags);

.pushsection .text
HwExceptionJacket:
    ## Preserve callee-saved registers on the stack for recovery later
    push %r15
    push %r14
    push %r13
    push %r12
    push %rbp
    push %rbx

    ## Backup previously saved pc, sp and jacket flags
    mov %gs:32, %r15
    push %r15
    mov %gs:40, %r15
    push %r15
    mov %gs:56, %r15
    push %r15

    ## Store recovery pc, sp and exceptions mask for recovery if needed.
    lea 1f(%rip), %rax
    mov %rax, %gs:32
    mov %rsp, %gs:40
    mov %rdx, %gs:56

    xchg %rsi, %rdi
    call *%rsi

    ## Success path

    ## Restore previously saved jacket flags, sp and pc values.
    pop %rax
    mov %rax, %gs:56
    pop %rax
    mov %rax, %gs:40
    pop %rax
    mov %rax, %gs:32

    pop %rbx
    pop %rbp
    pop %r12
    pop %r13
    pop %r14
    pop %r15

    ## Set rax to 0 since no exceptions fired.
    xor %eax, %eax
    ret

1: ## Failure path
    mov %rdi, %rsp

    ## The exception mask now contains the set of intercepted exceptions,
    ## leave it in rax as the return value.
    mov %gs:56, %rax

    ## Restore previously saved jacket flags, sp and pc values.
    pop %rcx
    mov %rcx, %gs:56
    pop %rcx
    mov %rcx, %gs:40
    pop %rcx
    mov %rcx, %gs:32

    pop %rbx
    pop %rbp
    pop %r12
    pop %r13
    pop %r14
    pop %r15

    ret

_EndOfHwExceptionJacket:
.popsection
