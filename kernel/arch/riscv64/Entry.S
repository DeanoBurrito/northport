.extern KernelEntry
.extern KERNEL_BOOT_BEGIN
.extern PHYS_BASE
.extern VIRT_BASE

.global SbiEntry
.global bootStackTop

.section .boottext, "ax", @progbits
SbiEntry:
    # disable interrupts and SUM
    li t0, 0x40022
    csrc sstatus, t0

    # This next part is to handle the undefined storm that is booting on multicore systems:
    # Some systems may boot each hart running the same code, others may only boot a single.
    # We dont know until the system starts up, so we use a lock to determine who is the bsp.
    # The first hart to take this lock is the bsp, and the rest wait until we bring them online
    # later using sbi.
    la t0, entryData + 0x20
    li t1, 1
    amoswap.w.aq t2, t1, (t0)
    bnez t2, Deadloop

    # stash the dtb and hart id, as well as the load addresses
    la t0, entryData
    sd a0, 0(t0)
    sd a1, 8(t0)
    la t1, PHYS_BASE
    sd t1, 16(t0)
    lui t1, %hi(VIRT_BASE)
    addi t1, t1, %lo(VIRT_BASE)
    sd t1, 24(t0)

    # Load the stack for C++land.
    mv fp, zero
    lui sp, %hi(bootStackTop)
    addi sp, sp, %lo(bootStackTop)

    # fallthrough
DetectPaging:
    # create an identity map we can use for sv48/sv57
    mv a0, zero
    li a1, 1
    mv a2, zero
    mv a3, zero
    la a4, table0
    li a5, 0xE # r + w + x
    jal AdjustPageTable

    la a1, table0
    li a2, 1

    # test for sv57 support
    li a0, 10
    jal TestPagingMode
    bnez a0, SetupSv57

    # test for sv48 support
    li a0, 9
    jal TestPagingMode
    bnez a0, SetupSv48

    # sv39 is the base mode, and should always exist
    j SetupSv39

SetupSv57:
    li a0, 1
    mv a1, zero
    jal SvCommonSetup

    la a0, table2
    jal MapKernel

    li a0, 0xFF8 # map table2 as the last PTE in table1
    li a1, 1
    la a2, table2
    # a3 unused when mapping a single page
    la a4, table1
    mv a5, zero
    jal AdjustPageTable

    li a0, 0xFF8 # map table1 as last PTE in table0
    li a1, 1
    la a2, table1
    la a4, table0
    mv a5, zero
    jal AdjustPageTable

    li a0, 10
    la a1, table0
    mv a2, zero
    jal TestPagingMode

    j JumpToMain

SetupSv48:
    li a0, 1
    mv a1, zero
    jal SvCommonSetup

    la a0, table1
    jal MapKernel

    li a0, 0xFF8 # table0.entries[511] = table1
    li a1, 1
    la a2, table1
    la a4, table0
    mv a5, zero
    jal AdjustPageTable

    li a0, 9
    la a1, table0
    mv a2, zero
    jal TestPagingMode

    j JumpToMain

SetupSv39:
    li a0, 16
    li a1, 0x40000000 # 1 GiB
    jal SvCommonSetup
    
    la a0, table0
    jal MapKernel

    li a0, 8
    la a1, table0
    mv a2, zero
    jal TestPagingMode

    j JumpToMain

JumpToMain:
    # cheeky jump to arbitary address using sepc + sret
    lui t0, %hi(KernelEntry)
    addi t0, t0, %lo(KernelEntry)
    csrw sepc, t0
    
    li t0, 0x100 # ensure SPP (prev priv) is set, so we sret to s-mode.
    csrs sstatus, t0
    la a0, entryData
    sret 

# a0: PTE entry offset, a1: number of pages to map, a2: phys base,
# a3: phys page size, a4: page table address, a5: flags
AdjustPageTable:
    mv t1, a4
    add t1, t1, a0
    mv t2, zero # t2 = current count
    mv t4, a2
    mv t5, a5
    ori t5, t5, 1 # ensure valid bit is set
    beqz a5, 1f
    ori t5, t5, 0xC0 # if its a leaf, set D + A bits.
1:
    srli t3, t4, 2
    or t3, t3, t5
    sd t3, (t1)

    add t4, t4, a3
    addi t1, t1, 8
    addi t2, t2, 1
    blt t2, a1, 1b

    ret

# Loads the satp with a mode + PT root, can return if load was successful or not.
# a0: satp mode, a1: table addr, a2: test-only
TestPagingMode:
    mv t0, a0
    slli t0, t0, 60
    mv t1, a1
    srli t1, t1, 12
    or t0, t0, t1
    csrw satp, t0
    sfence.vma zero, zero
    
    bnez a2, 1f
    ret
1:
    csrr t1, satp
    csrw satp, zero
    bne t0, t1, 1f

    # CSR accepted write, mode available!
    li a0, 1
    ret
1:
    # Write rejected, not available.
    li a0, 0
    ret

# Creates an identity map + HHDM in table0.
# a0: page count, a1: page size
SvCommonSetup:
    mv s0, ra
    mv a3, a1
    mv a1, a0

    mv a0, zero # identity map
    mv a2, zero
    la a4, table0
    li a5, 0xE # r/w/x
    jal AdjustPageTable

    li a0, 0x800 # hhdm
    jal AdjustPageTable

    mv ra, s0
    ret

# Maps the higher half portion of the kernel using 1GiB pages.
# a0: table addr
MapKernel:
    # Kernel gets loaded at -2 GiB, as per spec.
    mv a4, a0
    li a0, 0xFF0
    li a1, 2
    la a2, KERNEL_BOOT_BEGIN
    li t0, (0x40000000 - 1)
    not t0, t0
    and a2, a2, t0
    li a3, 0x40000000 # 1 GiB
    li a5, 0xE # r/w/x

    # we dont have a usable stack, so backup current return addr in s0
    mv s0, ra
    jal AdjustPageTable
    mv ra, s0
    ret

Deadloop:
    wfi
    j Deadloop

.section .bootdata, "aw", @nobits
.balign 0x1000
table0:
    .zero 0x1000
table1:
    .zero 0x1000
table2:
    .zero 0x1000
entryData:
    .quad 0 # bsp hart id
    .quad 0 # dtb address
    .quad 0 # phys load address
    .quad 0 # virt load address
    .quad 0 # bsp lock, first hart to set this is the bsp

.section .bss
bootStack:
    .zero 0x2000
bootStackTop:
