# This code is compiled once, and then copied once into memory for each interrupt we define (currently all of them).
# The 5 zero bytes are patched with a relative jump during the copy (since the exact value changes).
# There is also a second entry point to this code, which uses the static version compiled into the binary.
# That's used for executing ring changes and things like that, and is called like a function `DoInterruptReturn`.

# Used to calculate which areas of code to copy
.global InterruptStub_Begin
.global InterruptStub_End
.global InterruptStub_PatchCall

# Used to do a ring switch, takes primed stack as first arg (rdi)
.global DoInterruptReturn

# Using the outer labels we can store this once, and copy it as many times as we need at runtime
# NOTE: this does not push a dummy error code, or the vector number, we'll need to inject that ourselves

InterruptStub_Begin:
push %rax
push %rbx
push %rcx
push %rdx
push %rsi
push %rdi
# rsp is in the saved regs struct, but its meaningless here, we use iret rsp instead. Push a dummy value instead
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

# Check the current data segment, to see if we need to swap gs or not
mov %ds, %ax
cmp $0x10, %ax
je 1f
swapgs
1:

# Load kernel data segments, CS is set when jumping to an interrupt handler via the IDT selector field
mov $0x10, %rax
mov %ax, %ds
mov %ax, %ss
mov %ax, %es
mov %ax, %fs

# top of stack now points to base address of StoredRegisters struct (see Idt.h), setup for a c++ function
mov %rsp, %rdi

# this will get converted into `call imm32`, so we're just reserving space in the output binary
InterruptStub_PatchCall:
.zero 5

# We can get to this code from 2 paths:
# - by returning from the patched call above (5 reserved bytes). This happens when we return from an interrupt. We'll use the value in rax in this case
# - by calling DoInterruptReturn(), which puts the value in rdi. 
# Either way the stack gets loaded correctly, and we pop an interrupt frame.
mov %rax, %rsp
jmp 1f
DoInterruptReturn:
mov %rdi, %rsp
1:

# Load the appropriate data segments for the ring we're returning to. Assuming that iret_ss is valid here.
mov 0xB0(%rsp), %rax
mov %ax, %ds
mov %ax, %es
mov %ax, %fs

#change gs if we need to
cmp $0x10, %ax
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
# first pop into rdi is removing the padding for rsp (since its ignored), and second one is actual rdi value
pop %rdi
pop %rdi
pop %rsi
pop %rdx
pop %rcx
pop %rbx
pop %rax

# consume vector number and error code (always present)
add $16, %rsp
iretq

InterruptStub_End:
