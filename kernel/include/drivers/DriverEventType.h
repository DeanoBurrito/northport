#pragma once

#include <stdint.h>

namespace Kernel::Drivers
{
    enum class DriverEventType : uint64_t
    {
        //spurious, not quite sure what happened.
        Unknown = 0,
        //a VFS node was modified, argument is NodeChangedEventArgs* (see FilesystemDriver.h)
        FilesystemNodeChanged = 1,
        //the device fired an interrupt, argument is a size_t representing the interrupt vector.
        Interrupt = 2,
    };
}
