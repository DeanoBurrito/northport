#pragma once

#include <Types.h>

namespace sl
{
    using Elf32_Addr = uint32_t;
    using Elf32_Off = uint32_t;
    using Elf32_Half = uint16_t;
    using Elf32_Word = uint32_t;
    using Elf32_Sword = int32_t;
    using Elf32_UnsignedChar = uint8_t;

    struct [[gnu::packed]] Elf32_Ehdr
    {
        Elf32_UnsignedChar e_ident[16];
        Elf32_Half e_type;
        Elf32_Half e_machine;
        Elf32_Word e_version;
        Elf32_Addr e_entry;
        Elf32_Off e_phoff;
        Elf32_Off e_shoff;
        Elf32_Word e_flags;
        Elf32_Half e_ehsize;
        Elf32_Half e_phentsize;
        Elf32_Half e_phnum;
        Elf32_Half e_shentsize;
        Elf32_Half e_shnum;
        Elf32_Half e_shstrndx;
    };

    constexpr static Elf32_UnsignedChar ExpectedMagic[] = { 0x7F, 'E', 'L', 'F' };

    constexpr static Elf32_UnsignedChar EI_MAG0 = 0;
    constexpr static Elf32_UnsignedChar EI_MAG1 = 1;
    constexpr static Elf32_UnsignedChar EI_MAG2 = 2;
    constexpr static Elf32_UnsignedChar EI_MAG3 = 3;
    constexpr static Elf32_UnsignedChar EI_CLASS = 4;
    constexpr static Elf32_UnsignedChar EI_DATA = 5;
    constexpr static Elf32_UnsignedChar EI_VERSION = 6;
    constexpr static Elf32_UnsignedChar EI_PAD = 7;
    constexpr static Elf32_UnsignedChar EI_NIDENT = 16;

    constexpr static Elf32_UnsignedChar ELFCLASSNONE = 0;
    constexpr static Elf32_UnsignedChar ELFCLASS32 = 1;
    constexpr static Elf32_UnsignedChar ELFCLASS64 = 2;

    constexpr static Elf32_UnsignedChar ELFDATANONE = 0;
    constexpr static Elf32_UnsignedChar ELFDATA2LSB = 1;
    constexpr static Elf32_UnsignedChar ELFDATA2MSB = 2;

    constexpr static Elf32_Half ET_NONE = 0;
    constexpr static Elf32_Half ET_REL = 1;
    constexpr static Elf32_Half ET_EXEC = 2;
    constexpr static Elf32_Half ET_DYN = 3;
    constexpr static Elf32_Half ET_CORE = 4;
    constexpr static Elf32_Half ET_LOPROC = 0xFF00;
    constexpr static Elf32_Half ET_HIPROC = 0xFFFF;

    constexpr static Elf32_Half EM_NONE = 0;
    constexpr static Elf32_Half EM_M32 = 1;
    constexpr static Elf32_Half EM_SPARC = 2;
    constexpr static Elf32_Half EM_386 = 3;
    constexpr static Elf32_Half EM_68K = 4;
    constexpr static Elf32_Half EM_88K = 5;
    constexpr static Elf32_Half EM_860 = 7;
    constexpr static Elf32_Half EM_MIPS = 8;

    constexpr static Elf32_Word EV_CURRENT = 1;

    constexpr static Elf32_Half SHN_UNDEF = 0;
    constexpr static Elf32_Half SHN_LORESERVE = 0xFF00;
    constexpr static Elf32_Half SHN_LOPROC = 0xFF00;
    constexpr static Elf32_Half SHN_HIPROC = 0xFF1F;
    constexpr static Elf32_Half SHN_ABS = 0xFFF1;
    constexpr static Elf32_Half SHN_COMMON = 0xFFF2;
    constexpr static Elf32_Half SHN_HIRESERVE = 0xFFFF;

    struct [[gnu::packed]] Elf32_Shdr
    {
        Elf32_Word sh_name;
        Elf32_Word sh_type;
        Elf32_Word sh_flags;
        Elf32_Addr sh_addr;
        Elf32_Off sh_offset;
        Elf32_Word sh_size;
        Elf32_Word sh_link;
        Elf32_Word sh_info;
        Elf32_Word sh_addralign;
        Elf32_Word sh_entsize;
    };

    constexpr static Elf32_Word SHT_NULL = 0;
    constexpr static Elf32_Word SHT_PROGBITS = 1;
    constexpr static Elf32_Word SHT_SYMTAB = 2;
    constexpr static Elf32_Word SHT_STRTAB = 3;
    constexpr static Elf32_Word SHT_RELA = 4;
    constexpr static Elf32_Word SHT_HASH = 5;
    constexpr static Elf32_Word SHT_DYNAMIC = 6;
    constexpr static Elf32_Word SHT_NOTE = 7;
    constexpr static Elf32_Word SHT_NOBITS = 8;
    constexpr static Elf32_Word SHT_REL = 9;
    constexpr static Elf32_Word SHT_SHLIB = 10;
    constexpr static Elf32_Word SHT_DYNSYM = 11;
    constexpr static Elf32_Word SHT_LOPROC = 0x7000'0000;
    constexpr static Elf32_Word SHT_HIPROC = 0x7FFF'FFFF;
    constexpr static Elf32_Word SHT_LOUSER = 0x8000'0000;
    constexpr static Elf32_Word SHT_HIUSER = 0xFFFF'FFFF;

    constexpr static Elf32_Word SHF_WRITE = 1 << 0;
    constexpr static Elf32_Word SHF_ALLOC = 1 << 1;
    constexpr static Elf32_Word SHF_EXECINSTR = 1 << 2;
    constexpr static Elf32_Word SHF_MASKPROC = 0xF000'0000;

    struct [[gnu::packed]] Elf32_Sym
    {
        Elf32_Word st_name;
        Elf32_Addr st_value;
        Elf32_Word st_size;
        Elf32_UnsignedChar st_info;
        Elf32_UnsignedChar st_other;
        Elf32_Half st_shndx;
    };

#define ELF32_ST_BIND(i) ((i) >> 4)
#define ELF32_ST_TYPE(i) ((i) & 0xF)
#define ELF32_ST_INFO (b, t) (((b) << 4) + ((t) & 0xF))
#define ELF32_ST_VISIBILITY(o) ((o) & 0x3)

    constexpr static Elf32_UnsignedChar STB_LOCAL = 0;
    constexpr static Elf32_UnsignedChar STB_GLOBAL = 1;
    constexpr static Elf32_UnsignedChar STB_WEAK = 2;
    constexpr static Elf32_UnsignedChar STB_LOPROC = 13;
    constexpr static Elf32_UnsignedChar STB_HIPROC = 15;

    constexpr static Elf32_UnsignedChar STT_NOTYPE = 0;
    constexpr static Elf32_UnsignedChar STT_OBJECT = 1;
    constexpr static Elf32_UnsignedChar STT_FUNC = 2;
    constexpr static Elf32_UnsignedChar STT_SECTION = 3;
    constexpr static Elf32_UnsignedChar STT_FILE = 4;
    constexpr static Elf32_UnsignedChar STT_LOPROC = 13;
    constexpr static Elf32_UnsignedChar STT_HIPROC = 15;

    constexpr static Elf32_UnsignedChar STV_DEFAULT = 0;
    constexpr static Elf32_UnsignedChar STV_INTERNAL = 1;
    constexpr static Elf32_UnsignedChar STV_HIDDEN = 2;
    constexpr static Elf32_UnsignedChar STV_PROTECTED = 3;

    struct [[gnu::packed]] Elf32_Rel
    {
        Elf32_Addr r_offset;
        Elf32_Word r_info;
    };

    struct [[gnu::packed]] Elf32_Rela
    {
        Elf32_Addr r_offset;
        Elf32_Word r_info;
        Elf32_Sword r_addend;
    };

#define ELF32_R_SYM(i) ((i) >> 8)
#define ELF32_R_TYPE(i) ((uint8_t)(i))
#define ELF32_R_INFO(s, t) (((s) << 8) + (uint8_t)(t))

    constexpr static Elf32_Word R_68K_NONE = 0;
    constexpr static Elf32_Word R_68K_32 = 1;
    constexpr static Elf32_Word R_68K_16 = 2;
    constexpr static Elf32_Word R_68K_8 = 3;
    constexpr static Elf32_Word R_68K_PC32 = 4;
    constexpr static Elf32_Word R_68K_PC16 = 5;
    constexpr static Elf32_Word R_68K_PC8 = 6;
    constexpr static Elf32_Word R_68K_GOT32 = 7;
    constexpr static Elf32_Word R_68K_GOT16 = 8;
    constexpr static Elf32_Word R_68K_GOT8 = 9;
    constexpr static Elf32_Word R_68K_GOT32O = 10;
    constexpr static Elf32_Word R_68K_GOT16O = 11;
    constexpr static Elf32_Word R_68K_GOT8O = 12;
    constexpr static Elf32_Word R_68K_PLT32 = 13;
    constexpr static Elf32_Word R_68K_PLT16 = 14;
    constexpr static Elf32_Word R_68K_PLT8 = 15;
    constexpr static Elf32_Word R_68K_PLT32O = 16;
    constexpr static Elf32_Word R_68K_PLT16O = 17;
    constexpr static Elf32_Word R_68K_PLT8O = 18;
    constexpr static Elf32_Word R_68K_COPY = 19;
    constexpr static Elf32_Word R_68K_GLOB_DAT = 20;
    constexpr static Elf32_Word R_68K_JMP_SLOT = 21;
    constexpr static Elf32_Word R_68K_RELATIVE = 22;

    struct [[gnu::packed]] Elf32_Phdr
    {
        Elf32_Word p_type;
        Elf32_Off p_offset;
        Elf32_Addr p_vaddr;
        Elf32_Addr p_paddr;
        Elf32_Word p_filesz;
        Elf32_Word p_memsz;
        Elf32_Word p_flags;
        Elf32_Word p_align;
    };

    constexpr static Elf32_Word PT_NULL = 0;
    constexpr static Elf32_Word PT_LOAD = 1;
    constexpr static Elf32_Word PT_DYNAMIC = 2;
    constexpr static Elf32_Word PT_INTERP = 3;
    constexpr static Elf32_Word PT_NOTE = 4;
    constexpr static Elf32_Word PT_SHLIB = 5;
    constexpr static Elf32_Word PT_PHDR = 6;
    constexpr static Elf32_Word PT_LOPROC = 0x7000'0000;
    constexpr static Elf32_Word PT_HIPROC = 0x7FFF'FFFF;

    constexpr static Elf32_Word PF_X = 1 << 0;
    constexpr static Elf32_Word PF_W = 1 << 1;
    constexpr static Elf32_Word PF_R = 1 << 2;
    constexpr static Elf32_Word PF_MASKPROC = 0xFF00'0000;

    struct [[gnu::packed]] Elf32_Dyn
    {
        Elf32_Sword d_tag;
        union
        {
            Elf32_Word d_val;
            Elf32_Addr d_ptr;
        };
    };

    constexpr static Elf32_Sword DT_NULL = 0;
    constexpr static Elf32_Sword DT_NEEDED = 1;
    constexpr static Elf32_Sword DT_PLTRELSZ = 2;
    constexpr static Elf32_Sword DT_PLTGOT = 3;
    constexpr static Elf32_Sword DT_HASH = 4;
    constexpr static Elf32_Sword DT_STRTAB = 5;
    constexpr static Elf32_Sword DT_SYMTAB = 6;
    constexpr static Elf32_Sword DT_RELA = 7;
    constexpr static Elf32_Sword DT_RELASZ = 8;
    constexpr static Elf32_Sword DT_RELAENT = 9;
    constexpr static Elf32_Sword DT_STRSZ = 10;
    constexpr static Elf32_Sword DT_SYMENT = 11;
    constexpr static Elf32_Sword DT_INIT = 12;
    constexpr static Elf32_Sword DT_FINI = 13;
    constexpr static Elf32_Sword DT_SONAME = 14;
    constexpr static Elf32_Sword DT_RPATH = 15;
    constexpr static Elf32_Sword DT_SYMBOLIC = 16;
    constexpr static Elf32_Sword DT_REL = 17;
    constexpr static Elf32_Sword DT_RELSZ = 18;
    constexpr static Elf32_Sword DT_RELENT = 19;
    constexpr static Elf32_Sword DT_PLTREL = 20;
    constexpr static Elf32_Sword DT_DEBUG = 21;
    constexpr static Elf32_Sword DT_TEXTREL = 22;
    constexpr static Elf32_Sword DT_JMPREL = 23;
    constexpr static Elf32_Sword DT_LPROC = 0x7000'0000;
    constexpr static Elf32_Sword DT_HPROC = 0x7FFF'FFFF;
}
