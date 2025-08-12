# ---- Target Options ----
TARGET_ARCH = x86_64
TARGET_PLAT = pc
KERNEL_CXX_FLAGS += -Og -g
INCLUDE_TERMINAL_BG = no

# ---- Toolchain Options ----
TOOLCHAIN = clang
TOOLCHAIN_PREFIX =

# ---- Build System Options ----
QUIET_BUILD = yes
COUNT_TODOS = yes
KERNEL_UNITY_BUILD = yes
ENABLE_KERNAL_ASAN = no
ENABLE_KERNEL_ASLR = no
ENABLE_KERNEL_UBSAN = no
DEFAULT_TARGET = help-text
