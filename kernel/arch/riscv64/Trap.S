.global TrapEntry

.extern TrapDispatch
.extern suppressLogOutput

TrapEntry:
    csrrw x1, sscratch, x1 
    sd sp, 0(x1) # stash the original stack pointer

    # only load a new stack if we came from u-mode.
    csrr sp, sstatus
    andi sp, sp, 1 << 8
    bnez sp, 1f
    ld sp, 72(x1) # we came from u-mode, use kernel stack.
    j 2f
1:
    ld sp, 0(x1) # we came from s-mode, use existing stack.
2:
    addi sp, sp, 288 # make space on the stack for the trap frame.

    sd zero, 0(sp) # stack key, 0 = local frame
    sd x3, 0x18(sp)
    sd x4, 0x20(sp)
    sd x5, 0x28(sp)
    sd x6, 0x30(sp)
    sd x7, 0x38(sp)
    sd x8, 0x40(sp)
    sd x9, 0x48(sp)
    sd x10, 0x50(sp)
    sd x11, 0x58(sp)
    sd x12, 0x60(sp)
    sd x13, 0x68(sp)
    sd x14, 0x70(sp)
    sd x15, 0x78(sp)
    sd x16, 0x80(sp)
    sd x17, 0x88(sp)
    sd x18, 0x90(sp)
    sd x19, 0x98(sp)
    sd x20, 0xA0(sp)
    sd x21, 0xA8(sp)
    sd x22, 0xB0(sp)
    sd x23, 0xB8(sp)
    sd x24, 0xC0(sp)
    sd x25, 0xC8(sp)
    sd x26, 0xD0(sp)
    sd x27, 0xD8(sp)
    sd x28, 0xE0(sp)
    sd x29, 0xE8(sp)
    sd x30, 0xF0(sp)
    sd x31, 0xF8(sp)

    # suppress log output while we're in an interrupt
    li t0, 1
    la t2, suppressLogOutput
    sw t0, (t2)

    # store other details about the trap
    csrr t3, scause
    sd t3, 0x100(sp) # vector number
    csrr t0, stval
    sd t0, 0x108(sp) # error code
    csrr t1, sepc
    sd t1, 0x110(sp) # prev pc

    # stash spp and spie
    sd zero, 0x118(sp)
    csrr t0, sstatus
    li t2, 1
    andi t1, t0, 1 << 8
    beqz t1, 1f
    sw t2, 0x118(sp)
1:
    andi t1, t0, 1 << 5
    beqz t1, 1f
    sw t2, 0x11C(sp)
1:

    # store sp from before trap
    ld t0, 0(x1)
    sd t0, 0x10(sp)
    # restore core-local block and save original x1 value
    csrrw x1, sscratch, x1
    sd x1, 0x8(sp)

    mv a0, sp
    jal TrapDispatch
    mv sp, a0

    # restore log output
    mv t1, zero
    la t2, suppressLogOutput
    sw t1, (t2)

    ld t0, (sp)
    beqz t0, 1f

    # stack key is non-zero, reload satp and then continue trap return
    ld a0, (sp)
    ld a1, 8(sp)
    csrr a0, satp
    mv sp, a1 # load the actual stack
1:

    # setup spp and spie
    lw t1, 0x118(sp)
    li t0, 1 << 8
    beqz t1, 1f
    csrs sstatus, t0
    j 2f
1:
    csrc sstatus, t0
2:
    lw t1, 0x11C(sp)
    li t0, 1 << 5
    beqz t1, 1f
    csrs sstatus, t0
    j 2f
1:
    csrc sstatus, t0
2:
    
    # restore sepc
    ld t2, 0x110(sp)
    csrw sepc, t2

    # juggle x1 and sscratch
    ld x1, 0x8(sp)
    csrrw x1, sscratch, x1
    ld t0, 0x10(sp)
    sd t0, 0(x1)

    ld x3, 0x18(sp)
    ld x4, 0x20(sp)
    ld x5, 0x28(sp)
    ld x6, 0x30(sp)
    ld x7, 0x38(sp)
    ld x8, 0x40(sp)
    ld x9, 0x48(sp)
    ld x10, 0x50(sp)
    ld x11, 0x58(sp)
    ld x12, 0x60(sp)
    ld x13, 0x68(sp)
    ld x14, 0x70(sp)
    ld x15, 0x78(sp)
    ld x16, 0x80(sp)
    ld x17, 0x88(sp)
    ld x18, 0x90(sp)
    ld x19, 0x98(sp)
    ld x20, 0xA0(sp)
    ld x21, 0xA8(sp)
    ld x22, 0xB0(sp)
    ld x23, 0xB8(sp)
    ld x24, 0xC0(sp)
    ld x25, 0xC8(sp)
    ld x26, 0xD0(sp)
    ld x27, 0xD8(sp)
    ld x28, 0xE0(sp)
    ld x29, 0xE8(sp)
    ld x30, 0xF0(sp)
    ld x31, 0xF8(sp)

    ld x2, 0(x1) # load real sp
    csrrw x1, sscratch, x1 # restore the real x1
    sret