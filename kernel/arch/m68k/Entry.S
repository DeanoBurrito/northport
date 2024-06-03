.global ShimEntry

.extern ShimEntryNext

ShimEntry:
    clr %d0
    add #0x1234, %d0

    lea StackTop, %a7
    jmp ShimEntryNext

.section .bss
.align 16
StackBase:
.zero 0x4000
StackTop:
