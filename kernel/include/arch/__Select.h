#pragma once

/* The purpose of this file is to provide definitions for use in other arch layer headers,
 * otherwise we end up with a duplicate ifdef/elif/elif mess in each of those files.
 * Instead I've decided on a system where those check if a macro is defined, and if it is
 * they include that file. Then in this file we can glue those macros to arch/target definitions.
 */

/* Arch: x86_64 */
#if defined(__x86_64__)
#define NPK_ARCH_INCLUDE_INTERRUPTS <arch/x86_64/Interrupts.h>
#define NPK_ARCH_INCLUDE_MISC <arch/x86_64/Misc.h>

/* Arch: m68k */
#elif defined(__m68k__)

/* Arch: riscv64 */
#elif __riscv_xlen == 64

#endif
