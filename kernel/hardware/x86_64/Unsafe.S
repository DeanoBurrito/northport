.global UnsafeMemCopy
.global UnsafeMemSet

.type UnsafeMemCopy, @function
.size UnsafeMemCopy, (_EndOfUnsafeMemCopy - UnsafeMemCopy)
.type UnsafeMemSet, @function
.size UnsafeMemSet, (_EndOfUnsafeMemSet - UnsafeMemSet)

.pushsection .text
## `size_t UnsafeMemCopy(void* dest, const void* src, size_t len)`
UnsafeMemCopy:
    lea 1f(%rip), %rax
    mov %rax, %gs:32

    mov %rdx, %rcx
    rep movsb

    ## success
    xor %rax, %rax
    mov %rax, %gs:32
    mov %rdx, %rax
    ret

1:  ## failure
    xor %rax, %rax
    mov %rax, %gs:32
    sub %rdx, %rcx
    mov %rdx, %rax
    ret

_EndOfUnsafeMemCopy:

## `size_t UnsafeMemSet(void* dest, uint8_t value, size_t len)`
UnsafeMemSet:
    lea 1f(%rip), %rax
    mov %rax, %gs:32

    mov %rdx, %rcx
    mov %rsi, %rax
    rep stosb

    ## success
    xor %rax, %rax
    mov %rax, %gs:32
    mov %rdx, %rax
    ret

1:  ## failure
    xor %rax, %rax
    mov %rax, %gs:32
    sub %rdx, %rcx
    mov %rdx, %rax
    ret
_EndOfUnsafeMemSet:
.popsection
