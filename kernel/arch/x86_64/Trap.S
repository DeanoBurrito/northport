.global VectorStub0
.global TrapEntry

.extern TrapDispatch

TrapEntry:
    clac
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
    mov $0, %rbp
    call TrapDispatch

.altmacro
.macro CreateStubs first, last, ec=1
    .align 0x10
    VectorStub\first:
    .if \ec
        push $0x0
    .endif
    push $\first
    jmp TrapEntry
    .if \last-\first
        CreateStubs %\first+1, \last, \ec
    .endif
.endm

# Unfortunately using a static table of interrupt stubs does add 4KiB to
# our kernel binary size. 
CreateStubs 0, 7
CreateStubs 8, 8, 0
CreateStubs 9, 9
CreateStubs 10, 14, 0
CreateStubs 15, 16
CreateStubs 17, 17, 0
CreateStubs 18, 20
CreateStubs 21, 21, 0

# The rest of these are just populating the stubs, we have to do it like this
# because clang has a hard limit of 20 macro interations. GCC has a similar limit.
CreateStubs 22, 41
CreateStubs 42, 61
CreateStubs 62, 81
CreateStubs 82, 101
CreateStubs 102, 121
CreateStubs 122, 141
CreateStubs 142, 161
CreateStubs 162, 181
CreateStubs 182, 201
CreateStubs 202, 221
CreateStubs 222, 241
CreateStubs 242, 255
