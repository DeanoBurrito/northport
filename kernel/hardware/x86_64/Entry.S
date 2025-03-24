.global InterruptStubsBegin
.global RealEntry
.global BspStackTop

.extern KernelEntry

.pushsection .bss
.balign 0x1000
.zero 0x8000
BspStackTop:
.popsection

.type RealEntry, @function
.size RealEntry, (_EndOfRealEntry - RealEntry)

.pushsection .text
RealEntry:
    lea BspStackTop(%rip), %rsp
    call KernelEntry
_EndOfRealEntry:

.type InterruptEntry, @function
.size InterruptEntry, (_EndOfInterruptEntry - InterruptEntry)

InterruptEntry:
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15

    mov %es, %ax
    push %rax
    mov %ds, %ax
    push %rax

    cmp $0x10, %ax
    je 1f
    swapgs
1:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es

    mov %rsp, %rdi
    mov 152(%rsp), %rax
    push %rax
    cld
    call InterruptDispatch
    add $8, %rsp

    pop %rax
    cmp $0x10, %ax
    je 1f
    swapgs
1:
    mov %ax, %ds
    pop %rax
    mov %ax, %es

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
_EndOfInterruptEntry:
.popsection

.macro StubMacro vector
    .type VectorStub\vector,@function
    .size VectorStub\vector,(_EndOfVectorStub\vector - VectorStub\vector)
    .align 0x10

    VectorStub\vector:
    .if \vector < 32
        .if ((1 << \vector) & 0x60227D00) == 0
            sub $8, %rsp
        .endif
    .else
        sub $8, %rsp
    .endif
    push $\vector
    jmp InterruptEntry
    _EndOfVectorStub\vector:
.endm

.pushsection .rodata
.align 0x10
InterruptStubsBegin:
.altmacro
.set i, 0
.rept 256
StubMacro %i
.set i, i + 1
.endr
.popsection
