.global SwitchFrame

.type SwitchFrame,@function
.size SwitchFrame,(_EndOfSwitchFrame - SwitchFrame)

# void SwitchFrame(TrapFrame** current, TrapFrame* next)
SwitchFrame:
    move.l 8(%a7), %d1
    tst %d1
    bne 1f
    rts

1:
    move.l 4(%a7), %d0
    tst %d0
    beq SkipSave

    exg %d0, %a0
    move.l (%a7), %d0
    move.w #0, -(%a7)
    move.l %d0, -(%a7)
    move.w %sr, -(%a7)

    movem.l %d0-%a7, -(%a7)
    move.l %a7, (%a0)

SkipSave:
    exg %d1, %a7
    movem.l (%a7)+, %d0-%a7

    rte
_EndOfSwitchFrame:
