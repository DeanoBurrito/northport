#include <Logging.h>
#include <Format.h>
#include <SyscallFunctions.h>
#include <stdarg.h>

namespace np::Userland
{
    void Log(const sl::String& formatStr, LogLevel level, ...)
    {
        va_list args;
        va_start(args, level);
        const sl::String formatted = sl::FormatToStringV(formatStr, args);
        va_end(args);

        np::Syscall::Log(formatted, level);
    }
}
