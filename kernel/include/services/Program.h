#pragma once

#include <Types.h>

namespace Npk::Services
{
    enum class ExceptionType : unsigned
    {
        MemoryAccess = 0,
        InvalidInstruction = 1,
        BadOperation = 2,
        Breakpoint = 3,
    };

    struct ProgramException
    {
        uintptr_t pc;
        uintptr_t stack;
        uintptr_t special;
        unsigned flags;
        ExceptionType type;
    };

    const char* ExceptionName(ExceptionType type);
}
