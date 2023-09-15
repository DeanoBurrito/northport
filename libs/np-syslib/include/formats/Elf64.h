#pragma once

#include <stdint.h>

namespace sl
{
    using Elf64_Addr = uint64_t;
    using Elf64_Off = uint64_t;
    using Elf64_Half = uint16_t;
    using Elf64_Word = uint32_t;
    using Elf64_Sword = int32_t;
    using Elf64_Xword = uint64_t;
    using Elf64_Sxword = int64_t;
    using Elf64_UnsignedChar = uint8_t;

    struct [[gnu::packed]] Elf64_Ehdr
    {
        Elf64_UnsignedChar e_ident[16];
        Elf64_Half e_type;
        Elf64_Half e_machine;
        Elf64_Word e_version;
        Elf64_Addr e_entry;
        Elf64_Off e_phoff;
        Elf64_Off e_shoff;
        Elf64_Word e_flags;
        Elf64_Half e_ehsize;
        Elf64_Half e_phentsize;
        Elf64_Half e_phnum;
        Elf64_Half e_shentsize;
        Elf64_Half e_shnum;
        Elf64_Half e_shstrndx;
    };

    constexpr static Elf64_UnsignedChar ExpectedMagic[] = { 0x7F, 'E', 'L', 'F' };

    constexpr static Elf64_UnsignedChar EI_MAG0 = 0;
    constexpr static Elf64_UnsignedChar EI_MAG1 = 1;
    constexpr static Elf64_UnsignedChar EI_MAG2 = 2;
    constexpr static Elf64_UnsignedChar EI_MAG3 = 3;
    constexpr static Elf64_UnsignedChar EI_CLASS = 4;
    constexpr static Elf64_UnsignedChar EI_DATA = 5;
    constexpr static Elf64_UnsignedChar EI_VERSION = 6;
    constexpr static Elf64_UnsignedChar EI_OSABI = 7;
    constexpr static Elf64_UnsignedChar EI_ABIVERSION = 8;
    constexpr static Elf64_UnsignedChar EI_PAD = 9;
    constexpr static Elf64_UnsignedChar EI_NIDENT = 16;

    constexpr static Elf64_UnsignedChar ELFCLASS32 = 1;
    constexpr static Elf64_UnsignedChar ELFCLASS64 = 2;

    constexpr static Elf64_UnsignedChar ELFDATA2LSB = 1;
    constexpr static Elf64_UnsignedChar ELFDATA2MSB = 2;

    constexpr static Elf64_UnsignedChar ELFOSABI_SYSV = 0;
    constexpr static Elf64_UnsignedChar ELFOSABI_HPUX = 1;
    constexpr static Elf64_UnsignedChar ELFOSABI_STANDALONE = 255;
    
    constexpr static Elf64_Half ET_NONE = 0;
    constexpr static Elf64_Half ET_REL = 1;
    constexpr static Elf64_Half ET_EXEC = 2;
    constexpr static Elf64_Half ET_DYN = 3;
    constexpr static Elf64_Half ET_CORE = 4;
    constexpr static Elf64_Half ET_LOOS = 0xFE00;
    constexpr static Elf64_Half ET_HIOS = 0xFEFF;
    constexpr static Elf64_Half LOPROC = 0xFF00;
    constexpr static Elf64_Half HIPROC = 0xFFFF;

    constexpr static Elf64_Half EM_NONE = 0;
    constexpr static Elf64_Half EM_386 = 3;
    constexpr static Elf64_Half EM_ARM = 40;
    constexpr static Elf64_Half EM_IA_64 = 50;
    constexpr static Elf64_Half EM_X86_64 = 62;
    constexpr static Elf64_Half EM_AMD64 = EM_X86_64;
    constexpr static Elf64_Half EM_XTENSA = 94;
    constexpr static Elf64_Half EM_RISCV = 243;

    constexpr static Elf64_Word EV_CURRENT = 1;

    constexpr static Elf64_Half SHN_UNDEF = 0;
    constexpr static Elf64_Half SHN_LOPROC = 0xFF00;
    constexpr static Elf64_Half SHN_HIPROC = 0xFF1F;
    constexpr static Elf64_Half SHN_LOOS = 0xFF20;
    constexpr static Elf64_Half SHN_HIOS = 0xFF3F;
    constexpr static Elf64_Half SHN_ABS = 0xFFF1;
    constexpr static Elf64_Half SHN_COMMON = 0xFFF2;

    struct [[gnu::packed]] Elf64_Shdr
    {
        Elf64_Word sh_name;
        Elf64_Word sh_type;
        Elf64_Xword sh_flags;
        Elf64_Addr sh_addr;
        Elf64_Off sh_offset;
        Elf64_Xword sh_size;
        Elf64_Word sh_link;
        Elf64_Word sh_info;
        Elf64_Xword sh_addralign;
        Elf64_Xword sh_entsize;
    };

    constexpr static Elf64_Word SHT_NULL = 0;
    constexpr static Elf64_Word SHT_PROGBITS = 1;
    constexpr static Elf64_Word SHT_SYMTAB = 2;
    constexpr static Elf64_Word SHT_STRTAB = 3;
    constexpr static Elf64_Word SHT_RELA = 4;
    constexpr static Elf64_Word SHT_HASH = 5;
    constexpr static Elf64_Word SHT_DYNAMIC = 6;
    constexpr static Elf64_Word SHT_NOTE = 7;
    constexpr static Elf64_Word SHT_NOBITS = 8;
    constexpr static Elf64_Word SHT_REL = 9;
    constexpr static Elf64_Word SHT_SHLIB = 10;
    constexpr static Elf64_Word SHT_DYNSYM = 11;
    constexpr static Elf64_Word SHT_LOOS = 0x6000'0000;
    constexpr static Elf64_Word SHT_HIOS = 0x6FFF'FFFF;
    constexpr static Elf64_Word SHT_LOPROC = 0x7000'0000;
    constexpr static Elf64_Word SHT_HIPROC = 0x7FFF'FFFF;

    constexpr static Elf64_Xword SHF_WRITE = 1 << 0;
    constexpr static Elf64_Xword SHF_ALLOC = 1 << 1;
    constexpr static Elf64_Xword SHF_EXECINSTR = 1 << 2;
    constexpr static Elf64_Xword SHF_MASKOS = 0x0F00'0000;
    constexpr static Elf64_Xword SHF_MASKPROC = 0xF000'0000;

    struct [[gnu::packed]] Elf64_Sym
    {
        Elf64_Word st_name;
        Elf64_UnsignedChar st_info;
        Elf64_UnsignedChar st_other;
        Elf64_Half st_shndx;
        Elf64_Addr st_value;
        Elf64_Xword st_size;
    };

    constexpr static Elf64_UnsignedChar STB_LOCAL = 0;
    constexpr static Elf64_UnsignedChar STB_GLOBAL = 1;
    constexpr static Elf64_UnsignedChar STB_WEAK = 2;
    constexpr static Elf64_UnsignedChar STB_LOOS = 10;
    constexpr static Elf64_UnsignedChar STB_HIOS = 12;
    constexpr static Elf64_UnsignedChar STB_LOPROC = 13;
    constexpr static Elf64_UnsignedChar STB_HIPROC = 15;

    constexpr static Elf64_UnsignedChar STT_NOTYPE = 0;
    constexpr static Elf64_UnsignedChar STT_OBJECT = 1;
    constexpr static Elf64_UnsignedChar STT_FUNC = 2;
    constexpr static Elf64_UnsignedChar STT_SECTION = 3;
    constexpr static Elf64_UnsignedChar STT_FILE = 4;
    constexpr static Elf64_UnsignedChar STT_LOOS = 10;
    constexpr static Elf64_UnsignedChar STT_HIOS = 12;
    constexpr static Elf64_UnsignedChar STT_LOPROC = 13;
    constexpr static Elf64_UnsignedChar STT_HIPROC = 15;

    struct [[gnu::packed]] Elf64_Rel
    {
        Elf64_Addr r_offset;
        Elf64_Xword r_info;
    };

    struct [[gnu::packed]] Elf64_Rela
    {
        Elf64_Addr r_offset;
        Elf64_Xword r_info;
        Elf64_Sxword r_addend;
    };

#define ELF64_R_SYM(i) ((i) >> 32)
#define ELF64_R_TYPE(i) ((i) & 0xFFFF'FFFFl)
#define ELF64_R_INFO(s, t) ((s) << 32 + ((t) 0xFFFF'FFFFl))

    constexpr static Elf64_Word R_X86_64_NONE = 0;
    constexpr static Elf64_Word R_X86_64_64 = 1;
    constexpr static Elf64_Word R_X86_64_PC32 = 2;
    constexpr static Elf64_Word R_X86_64_GOT32 = 3;
    constexpr static Elf64_Word R_X86_64_PLT32 = 4;
    constexpr static Elf64_Word R_X86_64_COPY = 5;
    constexpr static Elf64_Word R_X86_64_GLOB_DAT = 6;
    constexpr static Elf64_Word R_X86_64_JUMP_SLOT = 7;
    constexpr static Elf64_Word R_X86_64_RELATIVE = 8;
    constexpr static Elf64_Word R_X86_64_GOTPCREL = 9;
    constexpr static Elf64_Word R_X86_64_32 = 10;
    constexpr static Elf64_Word R_X86_64_32S = 11;
    constexpr static Elf64_Word R_X86_64_16 = 12;
    constexpr static Elf64_Word R_X86_64_PC16 = 13;
    constexpr static Elf64_Word R_X86_64_8 = 14;
    constexpr static Elf64_Word R_X86_64_PC8 = 15;
    constexpr static Elf64_Word R_X86_64_DTPMOD64 = 16;
    constexpr static Elf64_Word R_X86_64_DTPOFF64 = 17;
    constexpr static Elf64_Word R_X86_64_TPOFF64 = 18;
    constexpr static Elf64_Word R_X86_64_TLSGD = 19;
    constexpr static Elf64_Word R_X86_64_TLSLD = 20;
    constexpr static Elf64_Word R_X86_64_DTPOFF32 = 21;
    constexpr static Elf64_Word R_X86_64_GOTTPOFF = 22;
    constexpr static Elf64_Word R_X86_64_TPOFF32 = 23;
    constexpr static Elf64_Word R_X86_64_PC64 = 24;
    constexpr static Elf64_Word R_X86_64_GOTOFF64 = 25;
    constexpr static Elf64_Word R_X86_64_GOTPC32 = 26;
    constexpr static Elf64_Word R_X86_64_GOT64 = 27;
    constexpr static Elf64_Word R_X86_64_GOTPCREL64 = 28;
    constexpr static Elf64_Word R_X86_64_GOTPC64 = 29;
    constexpr static Elf64_Word R_X86_64_PLTOFF64 = 31;
    constexpr static Elf64_Word R_X86_64_SIZE32 = 32;
    constexpr static Elf64_Word R_X86_64_SIZE64 = 33;
    constexpr static Elf64_Word R_X86_64_GOTPC32_TLSDESC = 34;
    constexpr static Elf64_Word R_X86_64_TLSDESC_CALL = 35;
    constexpr static Elf64_Word R_X86_64_TLSDESC = 36;
    constexpr static Elf64_Word R_X86_64_IRELATIVE = 37;
    constexpr static Elf64_Word R_X86_64_RELATIVE64 = 38;
    constexpr static Elf64_Word R_X86_64_GOTPCRELX = 41;
    constexpr static Elf64_Word R_X86_64_REX_GOTPCRELX = 42;

    constexpr static Elf64_Word R_RISCV_NONE = 0;
    constexpr static Elf64_Word R_RISCV_32 = 1;
    constexpr static Elf64_Word R_RISCV_64 = 2;
    constexpr static Elf64_Word R_RISCV_RELATIVE = 3;
    constexpr static Elf64_Word R_RISCV_COPY = 4;
    constexpr static Elf64_Word R_RISCV_JUMP_SLOT = 5;
    constexpr static Elf64_Word R_RISCV_TLS_DTPMOD32 = 6;
    constexpr static Elf64_Word R_RISCV_TLS_DTPMOD64 = 7;
    constexpr static Elf64_Word R_RISCV_TLS_DTPREL32 = 8;
    constexpr static Elf64_Word R_RISCV_TLS_DTPREL64 = 9;
    constexpr static Elf64_Word R_RISCV_TLS_TPREL32 = 10;
    constexpr static Elf64_Word R_RISCV_TLS_TPREL64 = 11;
    constexpr static Elf64_Word R_RISCV_BRANCH = 16;
    constexpr static Elf64_Word R_RISCV_JAL = 17;
    constexpr static Elf64_Word R_RISCV_CALL = 18;
    constexpr static Elf64_Word R_RISCV_CALL_PLT = 19;
    constexpr static Elf64_Word R_RISCV_GOT_HI20 = 20;
    constexpr static Elf64_Word R_RISCV_TLS_GOT_HI20 = 21;
    constexpr static Elf64_Word R_RISCV_TLS_GD_HI20 = 22;
    constexpr static Elf64_Word R_RISCV_PCREL_HI20 = 24;
    constexpr static Elf64_Word R_RISCV_PCREL_LO12_I = 24;
    constexpr static Elf64_Word R_RISCV_PCREL_LO12_S = 25;
    constexpr static Elf64_Word R_RISCV_HI20 = 26;
    constexpr static Elf64_Word R_RISCV_LO12_I = 27;
    constexpr static Elf64_Word R_RISCV_LO12_S = 28;
    constexpr static Elf64_Word R_RISCV_TPREL_HI20 = 29;
    constexpr static Elf64_Word R_RISCV_TPREL_LO12_I = 30;
    constexpr static Elf64_Word R_RISCV_TPREL_LO12_S = 31;
    constexpr static Elf64_Word R_RISCV_TPREL_ADD = 32;
    constexpr static Elf64_Word R_RISCV_ADD8 = 33;
    constexpr static Elf64_Word R_RISCV_ADD16 = 34;
    constexpr static Elf64_Word R_RISCV_ADD32 = 35;
    constexpr static Elf64_Word R_RISCV_ADD64 = 36;
    constexpr static Elf64_Word R_RISCV_SUB8 = 37;
    constexpr static Elf64_Word R_RISCV_SUB16 = 38;
    constexpr static Elf64_Word R_RISCV_SUB32 = 39;
    constexpr static Elf64_Word R_RISCV_SUB64 = 40;
    constexpr static Elf64_Word R_RISCV_ALIGN = 43;
    constexpr static Elf64_Word R_RISCV_RVC_BRANCH = 44;
    constexpr static Elf64_Word R_RISCV_RVC_JUMP = 45;
    constexpr static Elf64_Word R_RISCV_RVC_LUI = 46;
    constexpr static Elf64_Word R_RISCV_RELAX = 51;
    constexpr static Elf64_Word R_RISCV_SUB6 = 52;
    constexpr static Elf64_Word R_RISCV_SET6 = 53;
    constexpr static Elf64_Word R_RISCV_SET8 = 54;
    constexpr static Elf64_Word R_RISCV_SET16 = 55;
    constexpr static Elf64_Word R_RISCV_SET32 = 56;
    constexpr static Elf64_Word R_RISCV_32_PCREL = 57;
    constexpr static Elf64_Word R_RISCV_IRELATIVE = 58;

    struct [[gnu::packed]] Elf64_Phdr
    {
        Elf64_Word p_type;
        Elf64_Word p_flags;
        Elf64_Off p_offset;
        Elf64_Addr p_vaddr;
        Elf64_Addr p_paddr;
        Elf64_Xword p_filesz;
        Elf64_Xword p_memsz;
        Elf64_Xword p_align;
    };

    constexpr static Elf64_Word PT_NULL = 0;
    constexpr static Elf64_Word PT_LOAD = 1;
    constexpr static Elf64_Word PT_DYNAMIC = 2;
    constexpr static Elf64_Word PT_INTERP = 3;
    constexpr static Elf64_Word PT_NOTE = 4;
    constexpr static Elf64_Word PT_SHLIB = 5;
    constexpr static Elf64_Word PT_PHDR = 6;
    constexpr static Elf64_Word PT_LOOS = 0x6000'0000;
    constexpr static Elf64_Word PT_HIOS = 0x6FFF'FFFF;
    constexpr static Elf64_Word PT_LOPROC = 0x7000'0000;
    constexpr static Elf64_Word PT_HIPROC = 0x7FFF'FFFF;

    constexpr static Elf64_Word PF_X = 1 << 0;
    constexpr static Elf64_Word PF_W = 1 << 1;
    constexpr static Elf64_Word PF_R = 1 << 2;
    constexpr static Elf64_Word PF_MASKOS = 0x00FF'0000;
    constexpr static Elf64_Word PF_MASKPROC = 0xFF00'0000;

    struct [[gnu::packed]] Elf64_Dyn
    {
        Elf64_Sxword d_tag;

        union
        {
            Elf64_Xword d_val;
            Elf64_Addr d_ptr;
        };
    };

    constexpr static Elf64_Sxword DT_NULL = 0;
    constexpr static Elf64_Sxword DT_NEEDED = 1;
    constexpr static Elf64_Sxword DT_PLTRELSZ = 2;
    constexpr static Elf64_Sxword DT_PLTGOT = 3;
    constexpr static Elf64_Sxword DT_HASH = 4;
    constexpr static Elf64_Sxword DT_STRTAB = 5;
    constexpr static Elf64_Sxword DT_SYMTAB = 6;
    constexpr static Elf64_Sxword DT_RELA = 7;
    constexpr static Elf64_Sxword DT_RELASZ = 8;
    constexpr static Elf64_Sxword DT_RELAENT = 9;
    constexpr static Elf64_Sxword DT_STRSZ = 10;
    constexpr static Elf64_Sxword DT_SYMENT = 11;
    constexpr static Elf64_Sxword DT_INIT = 12;
    constexpr static Elf64_Sxword DT_FINI = 13;
    constexpr static Elf64_Sxword DT_SONAME = 14;
    constexpr static Elf64_Sxword DT_RPATH = 15;
    constexpr static Elf64_Sxword DT_SYMBOLIC = 16;
    constexpr static Elf64_Sxword DT_REL = 17;
    constexpr static Elf64_Sxword DT_RELSZ = 18;
    constexpr static Elf64_Sxword DT_RELENT = 19;
    constexpr static Elf64_Sxword DT_PLTREL = 20;
    constexpr static Elf64_Sxword DT_DEBUG = 21;
    constexpr static Elf64_Sxword DT_TEXTREL = 22;
    constexpr static Elf64_Sxword DT_JMPREL = 23;
    constexpr static Elf64_Sxword DT_BIND_NOW = 24;
    constexpr static Elf64_Sxword DT_INIT_ARRAY = 25;
    constexpr static Elf64_Sxword DT_FINI_ARRAY = 26;
    constexpr static Elf64_Sxword DT_INIT_ARRAYSZ = 27;
    constexpr static Elf64_Sxword DT_FINI_ARRAYSZ = 28;
    constexpr static Elf64_Sxword DT_LOOS = 0x6000'0000;
    constexpr static Elf64_Sxword DT_HIOS = 0x6FFF'FFFF0;
    constexpr static Elf64_Sxword DT_LPROC = 0x7000'0000;
    constexpr static Elf64_Sxword DT_HPROC = 0x7FFF'FFFF0;
}

