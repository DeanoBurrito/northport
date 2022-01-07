#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Kernel::Devices
{
    void SetBootEpoch(uint64_t epoch);
    void IncrementUptime(size_t millis);
    uint64_t GetUptime();
}
