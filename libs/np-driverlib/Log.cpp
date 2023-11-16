#include <Log.h>
#include <drivers/api/Api.h>

#define NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS 0
#define NANOPRINTF_SNPRINTF_SAFE_TRIM_STRING_ON_OVERFLOW 1
//TODO: time to a way to manage external dependencies like nanoprintf, limine.h?, also single npf config

#define NANOPRINTF_IMPLEMENTATION
#include <debug/NanoPrintf.h>

namespace dl
{
    void Log(const char* str, LogLevel level, ...)
    {
        va_list argsList;
        va_start(argsList, level);
        const size_t length = npf_vsnprintf(nullptr, 0, str, argsList) + 1;
        va_end(argsList);

        char buffer[length];
        va_start(argsList, level);
        npf_vsnprintf(buffer, length, str, argsList);
        va_end(argsList);
        buffer[length - 1] = 0;

        npk_log(buffer, static_cast<npk_log_level>(level));
    }
}
