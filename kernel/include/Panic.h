#pragma once

#include <Platform.h>

namespace Kernel
{
    void InitPanic();

    [[noreturn]]
    void Panic(const char* reason, StoredRegisters* regs = nullptr);
}
