.global TrapEntry

.extern TrapDispatch

.type TrapEntry,@function
.size TrapEntry,(_EndOfTrapEntry - TrapEntry)

TrapEntry:
    movem.l %d0-%d7/%a0-%a7, -(%a7)

    move.l %a7, -(%a7)
    jsr TrapDispatch
_EndOfTrapEntry:
