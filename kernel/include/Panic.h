#pragma once

#include <Platform.h>

namespace Kernel
{
    void InitPanic();
    void SetPanicOverrideRegs(StoredRegisters* regs);

    [[noreturn]]
    void Panic(const char* reason);

    //NOTE: this function is not intended to be called normally, please dont do that.
    [[noreturn]]
    void PanicInternal(StoredRegisters* regs);
}
