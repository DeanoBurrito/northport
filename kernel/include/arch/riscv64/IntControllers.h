#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Optional.h>

namespace Npk
{
    void InitIntControllers();

    sl::Opt<size_t> HandleExternalInterrupt();
}
