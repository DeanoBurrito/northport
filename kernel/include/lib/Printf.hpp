#pragma once

#include <stdarg.h>
#include <lib/Compiler.hpp>
#include <lib/Types.hpp>

namespace sl
{
    using Putc = void (*)(int c, void* ctx);

    SL_PRINTF_FUNC(3, 4)
    int SnPrintf(char* buffer, size_t bufsz, const char* format, ...);

    SL_PRINTF_FUNC(3, 0)
    int VsnPrintf(char* buffer, size_t bufsz, const char* format,
        va_list vlist);

    SL_PRINTF_FUNC(3, 4)
    int PPrintf(Putc pc, void *pc_ctx, char const *format, ...);

    SL_PRINTF_FUNC(3, 0)
    int VpPrintf(Putc pc, void *pc_ctx, char const *format, va_list vlist);
}
