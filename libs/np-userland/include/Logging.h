#pragma once

#include <String.h>
#include <SyscallEnums.h>

using np::Syscall::LogLevel;

namespace np::Userland
{
    void Log(const sl::String& formatStr, LogLevel level, ...);
}
