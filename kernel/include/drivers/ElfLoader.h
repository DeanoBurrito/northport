#pragma once

#include <debug/Symbols.h>
#include <String.h>
#include <Handle.h>
#include <memory/VmObject.h>
#include <formats/Elf.h>
#include <Atomic.h>

namespace Npk::Drivers
{
    using ElfBlankFunc = void (*)();

    struct DynamicElfInfo
    {
        const char* strTable;
        const sl::Elf64_Sym* symTable;

        const void* pltRelocs;
        size_t pltRelocsSize;
        bool pltUsesRela;
        const sl::Elf64_Rela* relaEntries;
        size_t relaCount;
        const sl::Elf64_Rel* relEntries;
        size_t relCount;
    };

    struct LoadedElf
    {
        sl::Atomic<size_t> references;
        sl::Vector<VmObject> segments;
        sl::Handle<Debug::SymbolRepo> symbolRepo;
        uintptr_t loadBase;
        uintptr_t entryAddr;
    };

    sl::Opt<DynamicElfInfo> ParseDynamic(VmObject& file, uintptr_t loadBase);

    void ScanForModules(sl::StringSpan dirpath);
    bool ScanForDrivers(sl::StringSpan filepath);

    sl::Handle<LoadedElf> LoadElf(VMM* vmm, sl::StringSpan filepath, sl::StringSpan driverName);
}
