#define NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_SMALL_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_ALT_FORM_FLAG 1

#define NANOPRINTF_IMPLEMENTATION

#include <lib/Printf.hpp>
#include "NanoPrintf.h"

namespace sl
{
    int SnPrintf(char* buffer, size_t bufsz, const char* format, ...)
    {
        va_list vlist;

        va_start(vlist, format);
        auto ret = npf_vsnprintf(buffer, bufsz, format, vlist);
        va_end(vlist);

        return ret;
    }

    int VsnPrintf(char* buffer, size_t bufsz, const char* format,
        va_list vlist)
    {
        return npf_vsnprintf(buffer, bufsz, format, vlist);
    }

    int PPrintf(Putc pc, void *pc_ctx, char const *format, ...)
    {
        va_list vlist;

        va_start(vlist, format);
        auto ret = npf_vpprintf(pc, pc_ctx, format, vlist);
        va_end(vlist);

        return ret;
    }

    int VpPrintf(Putc pc, void *pc_ctx, char const *format, va_list vlist)
    {
        return npf_vpprintf(pc, pc_ctx, format, vlist);
    }
}
