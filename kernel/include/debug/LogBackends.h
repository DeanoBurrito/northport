#pragma once

#include <stddef.h>

namespace Npk::Debug
{
    enum class LogBackend : unsigned
    {
        Terminal = 0,
        Serial = 1,

        EnumCount,
    };

    void InitTerminal();
    void WriteTerminal(const char* str, size_t length);
    void InitSerial();
    void WriteSerial(const char* str, size_t length);

    void EnableLogBackend(LogBackend backend, bool enabled);
}
