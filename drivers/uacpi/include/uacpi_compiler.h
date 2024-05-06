#pragma once

/* Note: this file is only needed until uACPI explicitly supports riscv.
 * At that point, delete this file and use the builtin!
 */

#define UACPI_ALIGN(x) __declspec(align(x))
#define UACPI_ALWAYS_INLINE __attribute__((always_inline))
#define UACPI_PACKED(decl) decl __attribute__((packed));

#define uacpi_unlikely(expr) __builtin_expect(!!(expr), 0)
#define uacpi_likely(expr)   __builtin_expect(!!(expr), 1)

#if __has_attribute(__fallthrough__)
    #define UACPI_FALLTHROUGH __attribute__((__fallthrough__))
#endif

#define UACPI_MAYBE_UNUSED __attribute__ ((unused))

#define UACPI_NO_UNUSED_PARAMETER_WARNINGS_BEGIN             \
    _Pragma("GCC diagnostic push")                           \
    _Pragma("GCC diagnostic ignored \"-Wunused-parameter\"")

#define UACPI_NO_UNUSED_PARAMETER_WARNINGS_END \
    _Pragma("GCC diagnostic pop")

#ifdef __clang__
    #define UACPI_PRINTF_DECL(fmt_idx, args_idx) \
        __attribute__((format(printf, fmt_idx, args_idx)))
#else
    #define UACPI_PRINTF_DECL(fmt_idx, args_idx) \
        __attribute__((format(gnu_printf, fmt_idx, args_idx)))
#endif

#define UACPI_POINTER_SIZE 8
