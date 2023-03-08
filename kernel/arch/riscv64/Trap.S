.global TrapEntry
.global ExecuteTrapFrame

.extern TrapDispatch

TrapEntry:
    csrrw x1, sscratch, x1 
    sd sp, 0(x1) # stash the original stack pointer

    # only load a new stack if we came from u-mode.
    csrr sp, sstatus
    andi sp, sp, 1 << 8
    bne sp, zero, 1f
wtf_why_did_we_come_from_umode:
    j wtf_why_did_we_come_from_umode
    ld sp, 88(x1) # we came from u-mode, use kernel stack.
    j 2f
1:
    ld sp, 0(x1) # we came from s-mode, use existing stack.
2:
    addi sp, sp, -280 # make space on the stack for the trap frame.

    sd x3, 0x10(sp)
    sd x4, 0x18(sp)
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

    # stash spp and spie
    sd zero, 0x110(sp)
    csrr t0, sstatus
    li t2, 1
    andi t1, t0, 1 << 8
    beqz t1, 1f
    sw t2, 0x110(sp)
1:
    andi t1, t0, 1 << 5
    beqz t1, 1f
    sw t2, 0x114(sp)
1:

    # store sp from before trap
    ld t0, 0(x1)
    sd t0, 0x8(sp)
    # restore core-local block and save original x1 value
    csrrw x1, sscratch, x1
    sd x1, 0x0(sp)

    mv a0, sp
    jal TrapDispatch
