.global LoaderEntry
.global LoaderExit

.extern LoaderEntryNext

LoaderEntry:
    lea StackTop, %a7
    jmp LoaderEntryNext

# void LoaderExit(uintptr_t hhdmbase, uintptr_t jumpTarget)
LoaderExit:
    move.l 8(%a7), %a0

    lea StackTop, %a1
    add.l 4(%a7), %a1
    exg %a1, %a7

    jmp (%a0)

.section .bss
.align 16
StackBase:
.zero 0x4000
StackTop:
