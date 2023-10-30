.global Panic

.extern PanicLanding
.extern HatHandlePanic
.extern panicStack

# void Panic(const char* reason)
Panic:
    li t0, 0x40002
    csrc sstatus, t0
    mv s1, a0

    # Jump to known good stack, ensure MMU is happy
    lui sp, %hi(panicStack)
    addi sp, sp, %lo(panicStack)
    jal HatHandlePanic

    # Jump to common handler
    mv a0, s1
    jal PanicLanding
