.global UnsafeCopy

.set UNSAFE_OP_ABORT_OFFSET, 0xB0

.type UnsafeCopy,@function
.size UnsafeCopy,(_EndOfUnsafeCopy - UnsafeCopy)

## size_t UnsafeCopy(void* dest, const void* source, size_t count)
UnsafeCopy:
    lea 1f(%rip), %rax
    mov %rax, %gs:UNSAFE_OP_ABORT_OFFSET
    mov %rdx, %rcx
    rep movsb

    xor %rax, %rax
    mov %rax, %gs:UNSAFE_OP_ABORT_OFFSET
    mov %rdx, %rax
    ret

1:
    xor %rax, %rax
    mov %rax, %gs:UNSAFE_OP_ABORT_OFFSET

    sub %rcx, %rdx
    mov %rdx, %rax
    ret
_EndOfUnsafeCopy:
