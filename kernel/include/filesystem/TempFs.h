#pragma once

#include <filesystem/Filesystem.h>

namespace Npk::Filesystem
{
    /* A deceptively powerful function:
     * Creates a new tempFS, registers it with the driver manager so and returns
     * the ID associated with the fs driver. The tag argument is functionally useless,
     * but is displayed as part of the driver's `get_summary()` function,
     * which can be useful for debugging.
     */
    size_t CreateTempFs(sl::StringSpan tag);
}
