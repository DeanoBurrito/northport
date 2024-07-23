#pragma once

/* Arch: x86_64 */
#if defined(__x86_64__)
#define NPK_ARCH_INCLUDE_PLATFORM <arch/x86_64/Platform.h>
#define NPK_ARCH_INCLUDE_HAT <arch/x86_64/Hat.h>

/* Arch: m68k */
#elif defined(__m68k__)
#define NPK_ARCH_INCLUDE_PLATFORM <arch/m68k/Platform.h>
#define NPK_ARCH_INCLUDE_HAT <arch/m68k/Hat.h>

/* Arch: riscv64 */
#elif __riscv_xlen == 64
#define NPK_ARCH_INCLUDE_PLATFORM <arch/riscv64/Platform.h>
#define NPK_ARCH_INCLUDE_HAT <arch/riscv64/Hat.h>

#endif
