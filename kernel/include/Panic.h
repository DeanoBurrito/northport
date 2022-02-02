#pragma once

#include <Platform.h>

namespace Kernel
{
    void InitPanic();
    void SetPanicFramebuffer(void* linearFramebuffer);

    [[noreturn]]
    void Panic(const char* reason);
}
