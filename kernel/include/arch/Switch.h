#pragma once

#include <Span.h>

namespace Npk
{
    struct ExecFrame;

    //constructs an ExecFrame on the memory indicated by the `stack` variable
    ExecFrame* InitExecFrame(uintptr_t stack, uintptr_t entry, void* arg);

    //saves the current thread state (if `save` is non-null), switches stack to the one indicated
    //by `load`. If `callback` is non-null the callback function is called before restoring
    //the rest of the state indicated in `load`.
    void SwitchExecFrame(ExecFrame** save, ExecFrame* load, void (*callback)(void*), void* callbackArg) 
        asm("SwitchExecFrame");
}
