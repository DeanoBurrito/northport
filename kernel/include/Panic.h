#pragma once

#include <Span.h>
#include <stdint.h>

namespace Npk::Services
{
    struct ProgramException;
}

namespace Npk
{
    void PanicWithException(Services::ProgramException ex, uintptr_t traceStart);
    void PanicWithString(sl::StringSpan reason);
}
