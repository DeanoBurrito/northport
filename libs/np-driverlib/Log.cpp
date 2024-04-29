#include <Log.h>
#include <interfaces/driver/Api.h>
#include <NanoPrintf.h>

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

        npk_log({ .length = length - 1, .data = buffer,  }, static_cast<npk_log_level>(level));
    }
}
