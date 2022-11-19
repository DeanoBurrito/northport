# ---- Toolchain Options ----
# Your compiler of choice: 'gcc' or 'clang', I dont judge.
TOOLCHAIN = gcc
# If not clang, where to find the compiler.
TOOLCHAIN_PREFIX = ../cross-tools/bin
# Where to find the target sysroot.
TOOLCHAIN_SYSROOT = ../cross-tools/lib/gcc/$(ARCH_TARGET)
# Where to find your limine install.
LIMINE_DIR = ../cross-tools/limine

# ---- Emulator Options ----
BOOT_WITH_UEFI = yes

# ---- Build System Options ----
# 'yes' will suppress output of build commands to stdout, 'no' will output everything.
QUIET_BUILD = yes

# ---- Compiler Options ----
# ISA to compile for: 'x86_64' or 'riscv64'
export CPU_ARCH = x86_64
# KERNEL_CXX_FLAGS += -fsanitize=undefined
KERNEL_CXX_FLAGS += -O0 -g
# Include a nice background for the terminal or not.
INCLUDE_TERMINAL_BG = no
# Space reserved for the log bladder. If inside a trap handler or no backends are available,
# logs will be written here instead of being lost immediately. Cannot be zero.
LOGGING_BALLOON_SIZE = 0x8000
# Time (in milliseconds) between core clock ticks, range is 1 - 20 (1000hz - 50hz).
# Note that this only affects timekeeping, scheduler decisions and other time-based events.
# are not affected by this. Lowering this may reduce system stress on slower systems.
CLOCK_TICK_MS = 1

# ---- X86_64 Options ----
# Use debugcon (port 0xE9) as the x86_64 serial driver. Only enable this for virtual machines.
X86_64_ENABLE_DEBUGCON_E9 = yes

# ---- Riscv 64 Options ----
