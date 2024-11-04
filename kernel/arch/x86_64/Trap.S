.global VectorStub0x00
.global TrapEntry

.extern TrapDispatch

.type TrapEntry,@function
.size TrapEntry,(_EndOfTrapEntry - TrapEntry)

TrapEntry:
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

    # Swap gs/kernelgs if needed
    mov %ds, %ax
    cmp $0x10, %ax
    je 1f
    swapgs
1:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %ss
    mov %ax, %es
    mov %ax, %fs

    mov %rsp, %rdi
    mov 136(%rsp), %rax # create a stack frame, so it looks like the interrupt was called.
    push %rax
    cld
    call TrapDispatch
_EndOfTrapEntry:

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
    jmp TrapEntry
    _EndOfVectorStub\vector:
.endm

# Thanks JW (J White?) for the idea of using irpc
.irpc i, 0123456789abcdef
.irpc j, 0123456789abcdef
    StubMacro 0x\i\j
.endr
.endr
