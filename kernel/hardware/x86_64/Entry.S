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
.extern DebugEventOccured

.type RealEntry, @function
.size RealEntry, (_EndOfRealEntry - RealEntry)
.type InterruptEntry, @function
.size InterruptEntry, (_EndOfInterruptEntry - InterruptEntry)
.type DebugEventEntry, @function
.size DebugEventEntry, (_EndOfDebugEventEntry - DebugEventEntry)
.type SysCallEntry, @function
.size SysCallEntry, (_EndOfSysCallEntry - SysCallEntry)
.type SysEnterEntry, @function
.size SysEnterEntry, (_EndOfSysEnterEntry - SysEnterEntry)
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

    call DebugEventOccured

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

## AMD have brought many great things to x86, but syscall is not one of them.
## We enter the kernel without a valid stack set, and there is a race condition
## with exceptions occuring before we can execute `swapgs`.
## The solution is to assume the race condition is invisible in this codepath,
## and from inside the DB/BP/NMI/MCE/etc handlers we need another mechanism
## to verify which gs base msr is the kernel pointer. So for the purpose of
## `syscall` entry, we're ignoring any racey exceptions.
## Similar calling convention to `sysenter`: rcx=user pc, r11=user flags, rdx=unused,
## rdi/rsi/rax/rbx/r12/r13 are arguments and return values.
SysCallEntry:
    swapgs

    xchg %rsp, %gs:24
    push %rcx
    xor %rcx, %rcx
    xchg %rcx, %gs:24
    push %rcx
    push %r11
    push %rbp

    push %rdi
    push %rsi
    push %rax
    push %rbx
    push %r12
    push %r13

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es

    mov %rsp, %rdi
    mov $0, %rsi
    call SyscallDispatch

    mov $0x20, %ax
    mov %ax, %ds
    mov %ax, %es

    pop %r13
    pop %r12
    pop %rbx
    pop %rax
    pop %rsi
    pop %rdi

    pop %rbp
    pop %r11
    pop %rcx
    mov %rcx, %gs:24
    pop %rcx
    xchg %rsp, %gs:24

    swapgs
    sysretq
_EndOfSysCallEntry:

## Entry via `sysenter` is significantly easier since we have a kernel-controlled
## stack already. This instruction leaves the calling convention to the OS,
## I've opted to use the following: rdx=user pc, rcx=user stack, r11=user flags,
## rbp=user scratch reg, arguments and return values are passed in 
## rdi/rsi/rax/rbx/r12/r13
SysEnterEntry:
    swapgs

    push %rdx
    push %rcx
    push %r11
    push %rbp

    push %rdi
    push %rsi
    push %rax
    push %rbx
    push %r12
    push %r13

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es

    mov %rsp, %rdi
    mov $1, %rsi
    cld
    call SyscallDispatch

    mov $0x20, %ax
    mov %ax, %ds
    mov %ax, %es

    pop %r13
    pop %r12
    pop %rbx
    pop %rax
    pop %rsi
    pop %rdi

    pop %rbp
    popf

    swapgs
    sysexitq
_EndOfSysEnterEntry:

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
