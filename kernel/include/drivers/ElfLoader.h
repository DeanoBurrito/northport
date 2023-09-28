#pragma once

#include <String.h>
#include <Optional.h>
#include <memory/VmObject.h>
#include <formats/Elf.h>

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

    sl::Opt<DynamicElfInfo> ParseDynamic(VmObject& file, uintptr_t loadBase);

    void ScanForModules(sl::StringSpan dirpath);
    bool ScanForDrivers(sl::StringSpan filepath);

    //Loads a elf into an address space and returns the entry address if it has one.
    sl::Opt<uintptr_t> LoadElf(VMM& vmm, sl::StringSpan filepath);
}
