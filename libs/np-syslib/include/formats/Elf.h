#pragma once

#include <formats/Elf64.h>
#include <containers/Vector.h>

namespace sl
{
    bool ValidateElfHeader(const void* file, Elf64_Half type);

    struct ComputedReloc
    {
        uintptr_t value;
        size_t length;
        bool usedSymbol;
    };

    ComputedReloc ComputeRelocation(Elf64_Word type, uintptr_t a, uintptr_t b, uintptr_t s, uintptr_t p);

    sl::Vector<const Elf64_Phdr*> FindPhdrs(const Elf64_Ehdr* hdr, Elf64_Word type); 
    const Elf64_Shdr* FindShdr(const Elf64_Ehdr* hdr, const char* name);
    sl::Vector<const Elf64_Shdr*> FindShdrs(const Elf64_Ehdr* hdr, Elf64_Word type);
}

