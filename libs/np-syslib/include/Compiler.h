#pragma once

/* This header was written with c++17 in mind, so there are no macros for langauge features like
 * no-return, fallthrough or deprecated attribs.
 */

#ifdef __GNUC__
    #define SL_ALWAYS_INLINE [[gnu::always_inline]] inline
    #define SL_PACKED(x) [[gnu::packed]] x
    #define SL_PRINTF_FUNC(fmt, args) [[gnu::format(printf, fmt, args)]]
    #define SL_NAKED_FUNC [[gnu::naked]]
    #define SL_UNREACHABLE()  __builtin_unreachable()
    
    #define SL_LIKELY(x) __builtin_expect(!!(x), 1)
    #define SL_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #error "Failed to detect compiler."
#endif

#define SL_UNUSED_ARG(x) (void)(x)
