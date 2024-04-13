#pragma once

#include <Span.h>
#include <stdint.h>

namespace Npk::Tasking
{
    struct ProgramException;
}

namespace Npk::Debug
{
    void PanicWithException(Tasking::ProgramException, uintptr_t traceStart);
    void Panic(sl::StringSpan reason);
}
