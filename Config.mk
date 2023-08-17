# ---- Toolchain Options ----
# Your compiler of choice: 'gcc' or 'clang', I dont judge.
TOOLCHAIN = gcc
# If not clang, where to find the compiler.
TOOLCHAIN_PREFIX = ../cross-tools/bin
# Whether to use the development cache for bootloader and firmware files.
# This downloads and stores any necessary UEFI firmware for testing and the limine
# bootloader in `.devel-cache`. Disable this if you want to use your own
# firmware and copy of limine.
USE_DEVEL_CACHE = yes

# If you're not using the cache these paths will need to be populated
LIMINE_DIR =
OVMF_FILE =

# ---- Build System Options ----
# By default build commands are not echoed to stdout as there is a lot
# of information. Set this to 'no' if you are debugging the build system or 
# are interested in exactly what's being run.
QUIET_BUILD = yes
# Display the total count of TODOs within codebase after compile, 
# maybe shame me into fixing them oneday. Note this adds to compile times
# slightly, and can be worth disabling on less powerful machines.
COUNT_TODOS = yes
# Whether to include a copy of the kernel symbol table in the final executable.
# This is separate to the debug information `-g` adds.
KERNEL_SYMBOL_TABLE = yes

# ---- Compiler Options ----
# ISA to compile for, valid options: `x86_64`, `riscv64`.
CPU_ARCH = x86_64
# KERNEL_CXX_FLAGS += -fsanitize=undefined
KERNEL_CXX_FLAGS += -O0 -g
# Set to 'yes' to embed a nice terminal background into the kernel,
# this does significantly increase kernel size and build times. 
INCLUDE_TERMINAL_BG = no
# Resolution of the kernel clock in milliseconds. Range is 1-20ms (1000hz - 50hz).
CLOCK_TICK_MS = 1

# ---- X86_64 Options ----
# Enable use of debugcon (port 0xE9) as a serial driver and early log output. 
# Only enable this for virtual machines.
X86_64_ENABLE_DEBUGCON_E9 = yes
# Whether to boot qemu with legacy bios instead of OVMF.
X86_64_RUN_WITH_BIOS = no

# ---- Riscv 64 Options ----
# Meant as an equivalent to debugcon on x86, set to "no" to disable, or
# put the address the address of a uart controller the kernel can use to log.
RV64_ASSUME_UART = no
