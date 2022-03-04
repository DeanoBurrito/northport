.extern InterruptDispatch
.global InterruptStub_Begin
.global InterruptStub_End
.global InterruptStub_PatchCall

#Using the outer labels we can store this once, and copy it as many times as we need at runtime
#NOTE: this does not push a dummy error code, or the vector number, we'll need to inject that ourselves

InterruptStub_Begin:
push %rax
push %rbx
push %rcx
push %rdx
push %rsi
push %rdi
#rsp is in the saved regs struct, but its meaningless here, we use iret rsp instead. Push a dummy value instead
pushq $0
push %rbp
push %r8
push %r9
push %r10
push %r11
push %r12
push %r13
push %r14
push %r15

#top of stack now points to base address of StoredRegisters struct (see Idt.h), setup for a c++ function
mov %rsp, %rdi

#this will get converted into `call imm32`, so we're just reserving space in the output binary
InterruptStub_PatchCall:
.byte 0
.long 0

#Dispatch returns the saved stack we should operate on (it can be the same)
mov %rax, %rsp

#Load the appropriate data segments for the ring we're returning to. Assuming that iret_ss is valid here.
mov 0xB0(%rsp), %rax
mov %ax, %ds
mov %ax, %es
mov %ax, %fs
#TODO: ensure gs is what we expect, leaving it alone for now

pop %r15
pop %r14
pop %r13
pop %r12
pop %r11
pop %r10
pop %r9
pop %r8
pop %rbp
#first pop into rdi is removing the padding for rsp (since its ignored), and second one is actual rdi value
pop %rdi
pop %rdi
pop %rsi
pop %rdx
pop %rcx
pop %rbx
pop %rax

#consume vector number and error code (always present)
add $16, %rsp
iretq

InterruptStub_End:
