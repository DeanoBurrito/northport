#pragma once

#include <stddef.h>

namespace Kernel::Boot
{
    //returns the platform-specific ID for the boot core.
    size_t InitPlatformArch();

    //sets up core local state for the current core.
    void InitCore(size_t id, size_t acpiId);

    //performs any final setup for the current core, beginning running scheduler.
    [[noreturn]]
    void ExitInitArch();
}
