#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Handle.h>
#include <Optional.h>
#include <Flags.h>
#include <memory/VmObject.h>
#include <containers/Vector.h>
#include <String.h>
#include <Atomic.h>

namespace Npk::Debug
{
    struct KernelSymbol
    {
        uintptr_t base;
        size_t length;
        sl::StringSpan name;
    };

    enum class SymbolFlag
    {
        Public = 0,
        Private = 1,
        NonFunction = 2,
        Kernel = 3,
    };

    struct SymbolRepo
    {
        sl::Atomic<size_t> references;
        sl::String name;
        VmObject stringTable;

        sl::Vector<KernelSymbol> publicFunctions; //functions that can be linked against
        sl::Vector<KernelSymbol> privateFunctions; //non-linkable functions
        sl::Vector<KernelSymbol> nonFunctions; //all other symbols
    };

    struct SymbolStats
    {
        size_t publicCount;
        size_t privateCount;
        size_t otherCount;
    };

    using SymbolFlags = sl::Flags<SymbolFlag>;

    SymbolStats GetSymbolStats();
    void LoadKernelSymbols();

    sl::Handle<SymbolRepo> LoadElfModuleSymbols(sl::StringSpan name, VmObject& file, uintptr_t loadBase);
    sl::Opt<KernelSymbol> SymbolFromAddr(uintptr_t addr, SymbolFlags flags, sl::StringSpan* repoName = nullptr);
    sl::Opt<KernelSymbol> SymbolFromName(sl::StringSpan name, SymbolFlags flags, sl::StringSpan* repoName = nullptr);
}
