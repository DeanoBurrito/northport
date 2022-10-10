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
KERNEL_CXX_FLAGS += -O2
# Include a nice background for the terminal or not.
INCLUDE_TERMINAL_BG = no

# ---- X86_64 Options ----
# Use debugcon (port 0xE9) as the x86_64 serial driver. Only enable this for virtual machines.
X86_64_ENABLE_DEBUGCON_E9 = yes

# ---- Riscv 64 Options ----
# Some platforms don't support the 'rdtime' instruction, instead reading from an external timer.
# To save a trap to m-mode and extra instructions you can enable this if you know your target has this
# behaviour. We'll try to read from the clint->time register instead (if it exists).
RV64_NO_RDTIME = no
