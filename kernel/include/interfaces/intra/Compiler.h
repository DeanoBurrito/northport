#pragma once

#if defined(__GNUC__) || defined(__clang__)
    #define ALWAYS_INLINE [[gnu::always_inline]] inline
    #define PRINTF_FUNCTION(fmt, args) [[gnu::format(printf, fmt, args)]]
    #define NAKED_FUNCTION [[gnu::naked]]
    #define PACKED_STRUCT [[gnu::packed]]
#else
    #warning "Unexpeted copiler - things may get funky."
    #define ALWAYS_INLINE inline
    #define PRINTF_FUNCTION(fmt, args)
    #define NAKED_FUNCTION
    #define PACKED_STRUCT
#endif
