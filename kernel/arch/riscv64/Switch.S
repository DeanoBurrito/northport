.global SwitchFrame

# void SwitchFrame(TrapFrame** current, TrapFrame* next)
SwitchFrame:
    bnez a1, 1f
    ret # just return if the next frame is null, we cant load that.
1:
    beqz a0, SkipSave
    mv t0, sp
    addi sp, sp, -280 # allow space on the stack for the frame

    sd t0, 0x8(sp)  # we only save the required registers here, this does
    sd gp, 0x10(sp) # result in junk being reload into the unsaved registers
    sd tp, 0x18(sp) # later on, but those are caller-saved so this is fine.
    sd s0, 0x38(sp)
    sd s1, 0x40(sp)
    sd s2, 0x88(sp)
    sd s3, 0x90(sp)
    sd s4, 0x98(sp)
    sd s5, 0xA0(sp)
    sd s6, 0xA8(sp)
    sd s7, 0xB0(sp)
    sd s8, 0xB8(sp)
    sd s9, 0xC0(sp)
    sd s10, 0xC8(sp)
    sd s11, 0xD0(sp)

    sd ra, 0x108(sp) # store return address
    li t0, 1
    sd t0, 0x110(sp) # spp = 1 + spie = 0, we came from s-mode with ints disabled.

    sd sp, (a0)

SkipSave:
    mv sp, a1

    # setup spp and spie
    lw t1, 0x110(sp)
    li t0, 1 << 8
    beqz t1, 1f
    csrs sstatus, t0
    j 2f
1:
    csrc sstatus, t0
2:
    lw t1, 0x114(sp)
    li t0, 1 << 5
    beqz t1, 1f
    csrs sstatus, t0
    j 2f
1:
    csrc sstatus, t0
2:
    
    # restore sepc
    ld t2, 0x108(sp)
    csrw sepc, t2

    # juggle x1 and sscratch
    ld x1, 0(sp)
    csrrw x1, sscratch, x1
    ld t0, 0x8(sp)
    sd t0, 0(x1)

    ld x3, 0x10(sp)
    ld x4, 0x18(sp)
    ld x5, 0x20(sp)
    ld x6, 0x28(sp)
    ld x7, 0x30(sp)
    ld x8, 0x38(sp)
    ld x9, 0x40(sp)
    ld x10, 0x48(sp)
    ld x11, 0x50(sp)
    ld x12, 0x58(sp)
    ld x13, 0x60(sp)
    ld x14, 0x68(sp)
    ld x15, 0x70(sp)
    ld x16, 0x78(sp)
    ld x17, 0x80(sp)
    ld x18, 0x88(sp)
    ld x19, 0x90(sp)
    ld x20, 0x98(sp)
    ld x21, 0xA0(sp)
    ld x22, 0xA8(sp)
    ld x23, 0xB0(sp)
    ld x24, 0xB8(sp)
    ld x25, 0xC0(sp)
    ld x26, 0xC8(sp)
    ld x27, 0xD0(sp)
    ld x28, 0xD8(sp)
    ld x29, 0xE0(sp)
    ld x30, 0xE8(sp)
    ld x31, 0xF0(sp)

    ld x2, 0(x1) # load real sp
    csrrw x1, sscratch, x1 # restore the real x1
    sret
