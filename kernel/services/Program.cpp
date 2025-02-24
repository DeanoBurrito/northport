#include <services/Program.h>
#include <arch/Entry.h>

namespace Npk::Services
{
    constexpr const char* ExceptionNameStrs[] =
    {
        "memory access",
        "invalid instruction",
        "bad operation",
        "breakpoint",
    };

    const char* ExceptionName(ExceptionType type)
    {
        const unsigned index = static_cast<unsigned>(type);
        if (index >= sizeof(ExceptionNameStrs) / sizeof(const char*))
            return "<unknown exception type>";
        
        return ExceptionNameStrs[index];
    }
}

namespace Npk
{
    void DispatchException(ExceptionFrame* frame)
    {
    }
}
