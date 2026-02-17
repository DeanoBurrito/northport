.global InterruptStubsBegin
.global DebugEventEntry
.global RealEntry
.global BspStackTop
.global SysCallEntry
.global SysEnterEntry
.global BadSysCallEntry

.extern KernelEntry
.extern InterruptDispatch
.extern SyscallDispatch
.extern DebugEventOccurred

.type RealEntry, @function
.size RealEntry, (_EndOfRealEntry - RealEntry)
.type InterruptEntry, @function
.size InterruptEntry, (_EndOfInterruptEntry - InterruptEntry)
.type DebugEventEntry, @function
.size DebugEventEntry, (_EndOfDebugEventEntry - DebugEventEntry)
.type SysCallEntry, @function
.size SysCallEntry, (_EndOfSysCallEntry - SysCallEntry)
.type BadSysCallEntry, @function
.size BadSysCallEntry, (_EndOfBadSysCallEntry - BadSysCallEntry)

.pushsection .bss
.balign 0x1000
.zero 0x8000
BspStackTop:
.popsection

.pushsection .text
RealEntry:
    lea BspStackTop(%rip), %rsp
    call KernelEntry
_EndOfRealEntry:

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

    mov %cs, %ax
    cmp $0x8, %ax
    je 1f
    swapgs
1:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es

    mov %rsp, %rdi
    cld
    call InterruptDispatch

    mov 144(%rsp), %ax
    cmp $0x8, %ax
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
_EndOfInterruptEntry:

## No funny handling needed for debugger entries, since they are only called
## kernel code.
DebugEventEntry:
    push %rbx
    push %rcx
    push %rdx
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15

    call DebugEventOccurred

    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdx
    pop %rcx
    pop %rbx

    iretq
_EndOfDebugEventEntry:

SysCallEntry:
    ## get kernel values into segment registers (not necessary, but I like to
    ## know what they contain). The `syscall` instruction does smush values into
    ## the descriptor cache of %SS - see my previous comment.
    swapgs
    mov $0x10, %rbp
    mov %bp, %ss
    mov %bp, %ds
    mov %bp, %es
    mov %bp, %fs

    mov %rsp, %r12 ## backup user sp
    mov %gs:24, %rsp
    pop %rbx ## previously saved return address
    pop %rbp ## previously saved buffer for exit frame details

    ## Save syscall arguments
    mov %rax, 0x08(%rbp)
    mov %rdi, 0x10(%rbp)
    mov %rsi, 0x18(%rbp)
    mov %rdx, 0x20(%rbp)
    mov %r10, 0x28(%rbp)
    mov %r8,  0x30(%rbp)
    mov %r9,  0x38(%rbp)

    ## user pc, populated by `syscall` instruction
    mov %rcx, 0x40(%rbp)
    ## user sp, backed up earlier
    mov %r12, 0x48(%rbp)
    ## see `enum class HwUserExitType` for what this means
    movq $3,  0x00(%rbp)
    push %rsp

    push %rbx
    ret
_EndOfSysCallEntry:

BadSysCallEntry:
ud2
_EndOfBadSysCallEntry:
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

.pushsection .text
.align 0x10
InterruptStubsBegin:
.altmacro
.set i, 0
.rept 256
StubMacro %i
.set i, i + 1
.endr
.popsection
