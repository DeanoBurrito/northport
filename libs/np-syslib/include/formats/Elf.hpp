#pragma once

#include <Compiler.hpp>

#if __SIZEOF_POINTER__ == 8
    #include <formats/Elf64.hpp>

    #define Elf_Addr Elf64_addr
    #define Elf_Off Elf64_Off
    #define Elf_Half Elf64_Half
    #define Elf_Word Elf64_Word
    #define Elf_Sword Elf64_Sword
    #define Elf_Xword Elf64_Xword
    #define Elf_Sxword Elf64_Sxword
    #define Elf_UnsignedChar Elf64_UnsignedChar
    #define Elf_Ehdr Elf64_Ehdr
    #define Elf_Shdr Elf64_Shdr
    #define Elf_Sym Elf64_Sym
    #define Elf_Rel Elf64_Rel
    #define Elf_Rela Elf64_Rela
    #define Elf_Phdr Elf64_Phdr
    #define Elf_Dyn Elf64_Dyn

    #define ELF_ST_BIND ELF64_ST_BIND
    #define ELF_ST_TYPE ELF64_ST_TYPE
    #define ELF_ST_INFO ELF64_ST_INFO
    #define ELF_ST_VISIBILITY ELF64_ST_VISIBILITY
    #define ELF_R_TYPE ELF64_R_TYPE
    #define ELF_R_SYM ELF64_R_SYM
    #define ELF_R_INFO ELF64_R_INFO

    #define PRIelfRelInfo PRIx64
#elif __SIZEOF_POINTER__ == 4
    #include <formats/Elf32.hpp>

    #define Elf_Addr Elf32_addr
    #define Elf_Off Elf32_Off
    #define Elf_Half Elf32_Half
    #define Elf_Word Elf32_Word
    #define Elf_Sword Elf32_Sword
    #define Elf_Xword Elf32_Xword
    #define Elf_Sxword Elf32_Sxword
    #define Elf_UnsignedChar Elf32_UnsignedChar
    #define Elf_Ehdr Elf32_Ehdr
    #define Elf_Shdr Elf32_Shdr
    #define Elf_Sym Elf32_Sym
    #define Elf_Rel Elf32_Rel
    #define Elf_Rela Elf32_Rela
    #define Elf_Phdr Elf32_Phdr
    #define Elf_Dyn Elf32_Dyn

    #define ELF_ST_BIND ELF32_ST_BIND
    #define ELF_ST_TYPE ELF32_ST_TYPE
    #define ELF_ST_INFO ELF32_ST_INFO
    #define ELF_ST_VISIBILITY ELF32_ST_VISIBILITY
    #define ELF_R_TYPE ELF32_R_TYPE
    #define ELF_R_SYM ELF32_R_SYM
    #define ELF_R_INFO ELF32_R_INFO

    #define PRIelfRelInfo PRIx32
#else
    #error "Unsupported ELF spec"
#endif

namespace sl
{
    Elf_UnsignedChar ElfCurrentClass();
    Elf_UnsignedChar ElfCurrentData();
    Elf_Half ElfCurrentMachine();
    Elf_UnsignedChar ElfCurrentVersion();

    SL_ALWAYS_INLINE
    bool ValidElfForCurrentSystem(Elf_Ehdr* ehdr)
    {
        if (ehdr == nullptr)
            return false;
        for (size_t i = 0; i < 4; i++)
        {
            if (ehdr->e_ident[i] != ExpectedMagic[i])
                return false;
        }
        if (ehdr->e_ident[EI_CLASS] != ElfCurrentClass())
            return false;
        if (ehdr->e_ident[EI_DATA] != ElfCurrentData())
            return false;
        if (ehdr->e_machine != ElfCurrentMachine())
            return false;
        if (ehdr->e_version != ElfCurrentVersion())
            return false;

        return true;
    }

    struct ComputedRelocation
    {
        uintptr_t value;
        size_t length;
    };

    ComputedRelocation ComputeRuntimeRelocation(Elf_Word type, uintptr_t a, 
        uintptr_t b, uintptr_t p, uintptr_t s, uintptr_t v, uintptr_t z);
}

