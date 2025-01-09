#pragma once

#include <RefCount.h>
#include <Span.h>
#include <Optional.h>
#include <containers/List.h>

namespace Npk::Services
{
    struct SymbolInfo
    {
        uintptr_t base;
        size_t length;
        sl::StringSpan name;
    };

    struct SymbolRepo
    {
        sl::RefCount refCount;
        sl::ListHook hook;

        uintptr_t base;
        size_t length;
        sl::StringSpan name;
        sl::Span<const char> stringTable;
        sl::Span<const SymbolInfo> pubFuncs;
        sl::Span<const SymbolInfo> privFuncs;
    };

    struct SymbolView
    {
        sl::Ref<SymbolRepo, &SymbolRepo::refCount> repo;
        const SymbolInfo* info;
    };

    void LoadKernelSymbols();

    sl::Opt<SymbolView> FindSymbol(uintptr_t addr);
    sl::Opt<SymbolView> FindSymbol(sl::StringSpan name);
    sl::Opt<uintptr_t> FindDriverApiSymbol(sl::StringSpan name, bool kernelOnly);
}

