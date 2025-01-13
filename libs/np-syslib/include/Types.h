#pragma once

#ifdef __GNUC__
    typedef __UINTPTR_TYPE__ uintptr_t;
    typedef __INT8_TYPE__ int8_t;
    typedef __INT16_TYPE__ int16_t;
    typedef __INT32_TYPE__ int32_t;
    typedef __INT64_TYPE__ int64_t;

    typedef __UINT8_TYPE__ uint8_t;
    typedef __UINT16_TYPE__ uint16_t;
    typedef __UINT32_TYPE__ uint32_t;
    typedef __UINT64_TYPE__ uint64_t;

    typedef __UINTMAX_TYPE__ uintmax_t;
    typedef __INTMAX_TYPE__ intmax_t;
    typedef __SIZE_TYPE__ size_t;
    typedef __PTRDIFF_TYPE__ ptrdiff_t;
    typedef ptrdiff_t ssize_t;

    #ifndef offsetof
        #define offsetof(s, m) __builtin_offsetof(s, m)
    #endif

    #if __SIZEOF_POINTER__ == 8
        #define NPK_P64 "l"
    #else
        #define NPK_P64 "ll"
    #endif
#else
    #error "Failed to detect compiler."
#endif

typedef decltype(nullptr) nullptr_t;
#ifndef NULL
    #define NULL ((void*)0)
#endif

#define PRIi8 "i"
#define PRIi16 "i"
#define PRIi32 "i"
#define PRIi64 NPK_P64"i"

#define PRIu8 "u"
#define PRIu16 "u"
#define PRIu32 "u"
#define PRIu64 NPK_P64"u"

#define PRIx8 "x"
#define PRIx16 "x"
#define PRIx32 "x"
#define PRIx64 NPK_P64"x"

#define PRIX8 "X"
#define PRIX16 "X"
#define PRIX32 "X"
#define PRIX64 NPK_P64"X"
