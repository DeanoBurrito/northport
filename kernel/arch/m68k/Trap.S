.global TrapEntry

.extern TrapDispatch

.type TrapEntry,@function
.size TrapEntry,(_EndOfTrapEntry - TrapEntry)

TrapEntry:
    movem.l %d0-%d7/%a0-%a7, -(%a7)
    move.l %a7, %a0

    move.l 62(%a7), %d0
    move.l 56(%a7), %d1
    move.l %d0, -(%a7)
    move.l %d1, -(%a7)

    move.l %a0, -(%a7)
    jsr TrapDispatch
_EndOfTrapEntry:
