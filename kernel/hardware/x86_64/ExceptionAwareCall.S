.global ExceptionAwareCall

.type ExceptionAwareCall, @function
.size ExceptionAwareCall, (_EndOfExceptionAwareCall - ExceptionAwareCall)

.pushsection .text
## `bool ExceptionAwareCall(void* a, void* b, void* c, void** r, 
##      void* (*func)(void* a, void* b, void* c));`
ExceptionAwareCall:
    ## Preserve all callee-saved registers for recovery later
    push %r15
    push %r14
    push %r13
    push %r12
    push %rbp
    push %rbx

    ## Store RIP and RSP for later if we need them
    mov %rsp, %gs:40
    lea 1f(%rip), %rax
    mov %rax, %gs:32

    push %rcx
    call *%r8
    pop %rcx

    test %rcx, %rcx
    jz 2f
    mov %rax, (%rcx)
2:
    ## Success path
    xor %eax, %eax
    mov %rax, %gs:40
    mov %rax, %gs:32
    add $(6 * 8), %rsp

    ret

1: ## Failure path
    xor %eax, %eax
    mov %rax, %gs:40
    mov %rax, %gs:32

    mov %rdi, %rsp
    pop %rbx
    pop %rbp
    pop %r12
    pop %r13
    pop %r14
    pop %r15

    mov $1, %eax
    ret

_EndOfExceptionAwareCall:
.popsection
