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
    #define SL_NO_PROFILE [[gnu::no_instrument_function]]
    #define SL_NO_KASAN [[gnu::no_sanitize_address]]
    #define SL_USED [[gnu::used]]

    #ifdef __ELF__
        #define SL_TAGGED(id, variable) [[gnu::section(".sl_tagged." #id)]] variable
        #define SL_SECTION(S, F) [[gnu::section(S)]] F
    #else
        #error "Unknown executable format"
    #endif

    #define SL_FILENAME_MACRO __FILE_NAME__
    #define SL_RETURN_ADDR __builtin_extract_return_addr(__builtin_return_address(0))
    
    #define SL_LIKELY(x) __builtin_expect(!!(x), 1)
    #define SL_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #error "Failed to detect compiler."
#endif

#define SL_UNUSED_ARG(x) (void)(x)

namespace sl
{
    SL_ALWAYS_INLINE
    void HintSpinloop()
    {
#ifdef __x86_64__
        asm("pause");
#else
    #error "Unsupported architecture"
#endif
    }
}
