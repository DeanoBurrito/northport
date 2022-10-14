.global SmpEntry

# .section .boottext, "ax", @progbits
.section .text

# a0 is the hart id, a1 is the opaque ptr. We use a1 to store
# the config for this core.
SmpEntry:
    li t0, 0x40002 # clear SIE and SUM
    csrc sstatus, t0

    ld a0, 16(a1) # limine_smp_info* for this hart
    ld sp, 0(a1) # stack
    ld t1, 8(a1) # copy of the bsp ptRoot
    csrw satp, t1

    # wait for goto_address to be written to
    mv t0, a0
    addi t0, t0, 16
1:
    # TODO: look into an riscv equivilent of 'pause'.
    ld t1, (t0)
    beqz t1, 1b

    mv ra, zero
    mv fp, zero
    jr t1

1:
    wfi
    j 1b
