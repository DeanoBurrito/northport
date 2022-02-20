#pragma once

#include <stdint.h>

namespace Kernel::Drivers
{
    enum class DriverEventType : uint64_t
    {
        Unknown = 0,
        FilesystemNodeChanged = 1,
    };
}
