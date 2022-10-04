# ---- Toolchain Options ----
TOOLCHAIN = gcc
TOOLCHAIN_PREFIX = ../cross-tools/bin
TOOLCHAIN_SYSROOT = ../cross-tools/lib/gcc/$(ARCH_TARGET)
LIMINE_DIR = ../cross-tools/limine

# ---- Emulator Options ----
BOOT_WITH_UEFI = yes

# ---- Build System Options ----
QUIET_BUILD = no

# ---- Compiler Options ----
# Arch to compile for: 'x86_64' or 'riscv64'
export CPU_ARCH = x86_64
# KERNEL_CXX_FLAGS += -fsanitize=undefined
KERNEL_CXX_FLAGS += -O0 -g
INCLUDE_TERMINAL_BG = yes

# ---- X86_64 Options ----
X86_64_ENABLE_DEBUGCON_E9 = yes

# ---- Riscv 64 Options ----
RV64_NO_RDTIME = no
