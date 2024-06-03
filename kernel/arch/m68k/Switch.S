.global SwitchFrame

.type SwitchFrame,@function
.size SwitchFrame,(_EndOfSwitchFrame - SwitchFrame)

# void SwitchFrame(TrapFrame** current, TrapFrame* next)
SwitchFrame:
    stop #0x2000
_EndOfSwitchFrame:
