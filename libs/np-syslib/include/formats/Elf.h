#pragma once

#include <formats/Elf64.h>
#include <containers/Vector.h>

namespace sl
{
    bool ValidateElfHeader(const void* file, Elf64_Half type);
    sl::Vector<const Elf64_Phdr*> FindPhdrs(const Elf64_Ehdr* hdr, Elf64_Word type); 
    const Elf64_Shdr* FindShdr(const Elf64_Ehdr* hdr, const char* name);
}

