#pragma once

#include <stddef.h>

namespace Npk::Debug
{
    enum class LogBackend : unsigned
    {
        Terminal = 0,
        Debugcon = 1,
        SerialNs16550 = 2,

        EnumCount,
    };

    bool InitTerminal();
    bool InitNs16550();

    void WriteTerminal(const char* str, size_t length);
    void WriteDebugcon(const char* str, size_t length);
    void WriteNs16550(const char* str, size_t length);

    void EnableLogBackend(LogBackend backend, bool enabled);
}
