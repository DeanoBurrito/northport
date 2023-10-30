.global Panic

.extern PanicLanding
.extern HatHandlePanic
.extern panicStack

# void Panic(const char* reason)
Panic:
    cli
    cld
    clac
    mov %rdi, %r15

    # Jump to a known good stack, ensure MMU is in a good state
    mov panicStack(%rip), %rsp
    call HatHandlePanic

    # It's safe to call the common handler now
    mov %r15, %rdi
    call PanicLanding
