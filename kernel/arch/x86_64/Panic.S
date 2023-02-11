.global Panic

.extern PanicLanding
.extern HatHandlePanic
.extern panicStack

# void Panic(TrapFrame* frame, const char* reason)
Panic:
    cli
    cld
    clac

    # Jump to a known good stack, ensure MMU is in a good state
    mov panicStack, %rsp
    call HatHandlePanic

    # It's safe to call the common handler now
    call PanicLanding