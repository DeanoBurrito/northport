#pragma once

#include <NativePtr.h>

namespace np::Syscall
{
    enum class SyscallId : NativeUInt
    {
        LoopbackTest = 0x0,
        GetPrimaryFramebuffer = 0x1,
    };
}
