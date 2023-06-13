.global TrapEntry
.global ExecuteTrapFrame

.extern TrapDispatch

# thanks to xvanc and sigterm for the idea of using tp to hold core-local info
# while in s-mode, and using sscratch as a backup for this register while in u-mode.
TrapEntry:
    csrrw tp, sscratch, tp
    bnez tp, FromUMode
FromSMode:
    csrrw tp, sscratch, tp # if we didn't jump we came from s-mode
    addi sp, sp, -280 # make space for the trap frame on the stack
    sd x1, 0x0(sp) # store x1 so we have a temporary register to work with

    # save the previous stack pointer
    mv x1, sp
    addi x1, x1, 280
    sd x1, 0x8(sp)

    li x1, 1 # set this frame to return to privilege 1 (s-mode)
    sd x1, 0x110(sp) # with interrupts disabled by default.

    csrr x1, sstatus # save interrupt enabled or not
    andi x1, x1, 1 << 5
    beqz x1, TrapContinue
    li x1, 1 # interrupts were enabled, store that info
    sw x1, 0x114(sp)
    j TrapContinue

FromUMode: # load a new stack from the core-local pointer and stash the interrupted %tp
    sd sp, 0(tp) # backup current stack in scratch space
    ld sp, 0x58(tp) # load kernel stack
    sd x1, 0(sp) # store x1 so we can use it as a temporary

    # save previous stack pointer
    ld x1, 0(tp)
    sd x1, 0x8(sp)
    
    sw zero, 0x110(sp) # set this frame to return to u-mode
    li x1, 1
    sw x1, 0x114(sp) # interrupts should enabled when returing to this frame

    csrrw x1, sscratch, x1 # store original value of tp
    sd x1, 0x18(sp)
    csrw sscratch, zero # zero sscratch, since we're in s-mode %tp contains this info
    j TrapContinue

TrapContinue:
    # store remaining GPRs: 
    # x0/zero doesn't need to be saved, for obvious reasons
    # x1/ra is used a temporary, and is saved above
    # x2/sp is already set by above
    sd x3, 0x10(sp)
    sd x5, 0x20(sp)
    sd x6, 0x28(sp)
    sd x7, 0x30(sp)
    sd x8, 0x38(sp)
    sd x9, 0x40(sp)
    sd x10, 0x48(sp)
    sd x11, 0x50(sp)
    sd x12, 0x58(sp)
    sd x13, 0x60(sp)
    sd x14, 0x68(sp)
    sd x15, 0x70(sp)
    sd x16, 0x78(sp)
    sd x17, 0x80(sp)
    sd x18, 0x88(sp)
    sd x19, 0x90(sp)
    sd x20, 0x98(sp)
    sd x21, 0xA0(sp)
    sd x22, 0xA8(sp)
    sd x23, 0xB0(sp)
    sd x24, 0xB8(sp)
    sd x25, 0xC0(sp)
    sd x26, 0xC8(sp)
    sd x27, 0xD0(sp)
    sd x28, 0xD8(sp)
    sd x29, 0xE0(sp)
    sd x30, 0xE8(sp)
    sd x31, 0xF0(sp)

    # store other details about the trap
    csrr t3, scause
    sd t3, 0xF8(sp) # vector number
    csrr t0, stval
    sd t0, 0x100(sp) # error code
    csrr t1, sepc
    sd t1, 0x108(sp) # prev pc

    mv a0, sp
    mv fp, zero
    jal TrapDispatch
