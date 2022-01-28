#pragma once

#include <Platform.h>
#include <scheduling/Scheduler.h>

namespace Kernel
{
    StoredRegisters* DispatchSyscall(StoredRegisters* regs);
}
