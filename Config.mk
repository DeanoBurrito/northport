# ---- Toolchain Options ----
TOOLCHAIN = gcc
TOOLCHAIN_PREFIX =

# ---- Build System Options ----
QUIET_BUILD = yes
KERNEL_UNITY_BUILD = yes
COUNT_TODOS = yes
ENABLE_KERNAL_ASAN = no
ENABLE_KERNEL_UBSAN = no
ENABLE_KERNEL_ASLR = no

# ---- Compiler Options ----
# Valid ISAs: 'x86_64', 'riscv64', 'm68k'
CPU_ARCH = x86_64
KERNEL_CXX_FLAGS += -Og -g
INCLUDE_TERMINAL_BG = no

# ---- Early Log Output Options ----
# These are per-platform and allow you to inform the kernel where it can assume
# a uart/serial chip can be found. These can be set to 'no' to disable them.
X86_ASSUME_DEBUGCON = yes
X86_ASSUME_COM1 = no
RV_ASSUME_NS16550 = no
M68K_ASSUME_NS16550 = no
