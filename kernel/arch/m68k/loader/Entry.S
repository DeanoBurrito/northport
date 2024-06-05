.global LoaderEntry

.extern LoaderEntryNext

LoaderEntry:
    lea StackTop, %a7
    jmp LoaderEntryNext

.section .bss
.align 16
StackBase:
.zero 0x4000
StackTop:
