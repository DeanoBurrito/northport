#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Optional.h>
#include <Span.h>

namespace Npk::Debug
{
    struct KernelSymbol
    {
        uintptr_t base;
        size_t length;
        sl::StringSpan name;
    };

    void LoadKernelSymbols();
    sl::Opt<KernelSymbol> SymbolFromAddr(uintptr_t addr);
    sl::Opt<KernelSymbol> SymbolFromName(sl::StringSpan name);
}
