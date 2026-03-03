#pragma once

#include <Types.hpp>

namespace Npk
{
    using Elf_Char = unsigned char;
    using Elf_Half = uint16_t;
    using Elf_Word = uint32_t;
    using Elf_Sword = int32_t;
    using Elf_Xword = uint64_t;
    using Elf_Sxword = int64_t;
    using Elf_Addr32 = uint32_t;
    using Elf_Addr64 = uint64_t;
    using Elf_Off32 = uint32_t;
    using Elf_Off64 = uint64_t;

    struct Elf_Ehdr
    {
        Elf_Char e_ident[16];
        Elf_Half e_type;
        Elf_Half e_machine;
        Elf_Word e_version;
    };

    struct Elf32_Ehdr
    {
        Elf_Char e_ident[16];
        Elf_Half e_type;
        Elf_Half e_machine;
        Elf_Word e_version;
        Elf_Addr32 e_entry;
        Elf_Off32 e_phoff;
        Elf_Off32 e_shoff;
        Elf_Word e_flags;
        Elf_Half e_ehsize;
        Elf_Half e_phentsize;
        Elf_Half e_phnum;
        Elf_Half e_shentsize;
        Elf_Half e_shnum;
        Elf_Half e_shstrndx;
    };

    struct Elf64_Ehdr
    {
        Elf_Char e_ident[16];
        Elf_Half e_type;
        Elf_Half e_machine;
        Elf_Word e_version;
        Elf_Addr64 e_entry;
        Elf_Off64 e_phoff;
        Elf_Off64 e_shoff;
        Elf_Word e_flags;
        Elf_Half e_ehsize;
        Elf_Half e_phentsize;
        Elf_Half e_phnum;
        Elf_Half e_shentsize;
        Elf_Half e_shnum;
        Elf_Half e_shstrndx;
    };

    constexpr Elf_Char ExpectedMagic[] = 
    { 
        0x7F, 
        'E', 
        'L', 
        'F' 
    };

    constexpr Elf_Char EI_MAG0 = 0;
    constexpr Elf_Char EI_MAG1 = 1;
    constexpr Elf_Char EI_MAG2 = 2;
    constexpr Elf_Char EI_MAG3 = 3;
    constexpr Elf_Char EI_CLASS = 4;
    constexpr Elf_Char EI_DATA = 5;
    constexpr Elf_Char EI_VERSION = 6;
    constexpr Elf_Char EI_OSABI = 7;
    constexpr Elf_Char EI_ABIVERSION = 8;
    constexpr Elf_Char EI_PAD = 9;
    constexpr Elf_Char EI_NIDENT = 16;

    constexpr Elf_Char ELFCLASSNONE = 0;
    constexpr Elf_Char ELFCLASS32 = 1;
    constexpr Elf_Char ELFCLASS64 = 2;

    constexpr Elf_Char ELFDATANONE = 0;
    constexpr Elf_Char ELFDATA2LSB = 1;
    constexpr Elf_Char ELFDATA2MSB = 2;

    constexpr Elf_Char ELFOSABI_NONE = 0;
    constexpr Elf_Char ELFOSABI_HPUX = 1;
    constexpr Elf_Char ELFOSABI_NETBSD = 2;
    constexpr Elf_Char ELFOSABI_GNU = 3;
    constexpr Elf_Char ELFOSABI_LINUX = 3;
    constexpr Elf_Char ELFOSABI_SOLARIS = 6;
    constexpr Elf_Char ELFOSABI_AIX = 7;
    constexpr Elf_Char ELFOSABI_IRIX = 8;
    constexpr Elf_Char ELFOSABI_FREEBSD = 9;
    constexpr Elf_Char ELFOSABI_TRU64 = 10;;
    constexpr Elf_Char ELFOSABI_MODESTO = 11;
    constexpr Elf_Char ELFOSABI_OPENBSD = 12;
    constexpr Elf_Char ELFOSABI_OPENVMS = 13;
    constexpr Elf_Char ELFOSABI_NSK = 14;
    constexpr Elf_Char ELFOSABI_AROS = 15;
    constexpr Elf_Char ELFOSABI_FENIXOS = 16;
    constexpr Elf_Char ELFOSABI_CLOUDABI = 17;
    constexpr Elf_Char ELFOSABI_OPENVOS = 18;
    constexpr Elf_Char ELFOSABI_STANDALONE = 255;

    constexpr Elf_Half ET_NONE = 0;
    constexpr Elf_Half ET_REL = 1;
    constexpr Elf_Half ET_EXEC = 2;
    constexpr Elf_Half ET_DYN = 3;
    constexpr Elf_Half ET_CORE = 4;
    constexpr Elf_Half ET_LOOS = 0xFE00;
    constexpr Elf_Half ET_HIOS = 0xFEFF;
    constexpr Elf_Half ET_LOPROC = 0xFF00;
    constexpr Elf_Half ET_HIPROC = 0xFFFF;

    constexpr Elf_Half EM_NONE = 0;
    constexpr Elf_Half EM_M32 = 1;
    constexpr Elf_Half EM_SPARC = 2;
    constexpr Elf_Half EM_386 = 3;
    constexpr Elf_Half EM_68K = 4;
    constexpr Elf_Half EM_88K = 5;
    constexpr Elf_Half EM_IAMCU = 6;
    constexpr Elf_Half EM_860 = 7;
    constexpr Elf_Half EM_MIPS = 8;
    constexpr Elf_Half EM_S370 = 9;
    constexpr Elf_Half EM_MIPS_RS3_LE = 10;
    constexpr Elf_Half EM_PARISC = 15;
    constexpr Elf_Half EM_VPP500 = 17;
    constexpr Elf_Half EM_SPARC32PLUS = 18;
    constexpr Elf_Half EM_960 = 19;
    constexpr Elf_Half EM_PPC = 20;
    constexpr Elf_Half EM_PPC64 = 21;
    constexpr Elf_Half EM_S390 = 22;
    constexpr Elf_Half EM_SPU = 23;
    constexpr Elf_Half EM_V800 = 36;
    constexpr Elf_Half EM_FR20 = 37;
    constexpr Elf_Half EM_RH32 = 38;
    constexpr Elf_Half EM_RCE = 39;
    constexpr Elf_Half EM_ARM = 40;
    constexpr Elf_Half EM_ALPHA = 41;
    constexpr Elf_Half EM_SH = 42;
    constexpr Elf_Half EM_SPARCV9 = 43;
    constexpr Elf_Half EM_TRICORE = 44;
    constexpr Elf_Half EM_ARC = 45;
    constexpr Elf_Half EM_H8_300 = 46;
    constexpr Elf_Half EM_H8_300H = 47;
    constexpr Elf_Half EM_H8S = 48;
    constexpr Elf_Half EM_H8_500 = 49;
    constexpr Elf_Half EM_IA_64 = 50;
    constexpr Elf_Half EM_MIPS_X = 51;
    constexpr Elf_Half EM_COLDFIRE = 52;
    constexpr Elf_Half EM_68HC12 = 53;
    constexpr Elf_Half EM_MMA = 54;
    constexpr Elf_Half EM_PCP = 55;
    constexpr Elf_Half EM_NCPU = 56;
    constexpr Elf_Half EM_NDR1 = 57;
    constexpr Elf_Half EM_STARCORE = 58;
    constexpr Elf_Half EM_ME16 = 59;
    constexpr Elf_Half EM_ST100 = 60;
    constexpr Elf_Half EM_TINYJ = 61;
    constexpr Elf_Half EM_X86_64 = 62;
    constexpr Elf_Half EM_AMD64 = EM_X86_64;
    constexpr Elf_Half EM_PDSP = 63;
    constexpr Elf_Half EM_PDP10 = 64;
    constexpr Elf_Half EM_PDP11 = 65;
    constexpr Elf_Half EM_FX66 = 66;
    constexpr Elf_Half EM_ST9PLUS = 67;
    constexpr Elf_Half EM_ST7 = 68;
    constexpr Elf_Half EM_68HC16 = 69;
    constexpr Elf_Half EM_68HC11 = 70;
    constexpr Elf_Half EM_68HC08 = 71;
    constexpr Elf_Half EM_68HC05 = 72;
    constexpr Elf_Half EM_SVX = 73;
    constexpr Elf_Half EM_ST19 = 74;
    constexpr Elf_Half EM_VAX = 75;
    constexpr Elf_Half EM_CRIS = 76;
    constexpr Elf_Half EM_JAVELIN = 77;
    constexpr Elf_Half EM_FIREPATH = 78;
    constexpr Elf_Half EM_ZSP = 79;
    constexpr Elf_Half EM_MMIX = 80;
    constexpr Elf_Half EM_HUANY = 81;
    constexpr Elf_Half EM_PRISM = 82;
    constexpr Elf_Half EM_AVR = 83;
    constexpr Elf_Half EM_FR30 = 84;
    constexpr Elf_Half EM_D10V = 85;
    constexpr Elf_Half EM_D30V = 86;
    constexpr Elf_Half EM_V850 = 87;
    constexpr Elf_Half EM_M32R = 88;
    constexpr Elf_Half EM_MN10300 = 89;
    constexpr Elf_Half EM_MN10200 = 90;
    constexpr Elf_Half EM_PJ = 91;
    constexpr Elf_Half EM_OPENRISC = 92;
    constexpr Elf_Half EM_ARC_COMPACT = 93;
    constexpr Elf_Half EM_XTENSA = 94;
    constexpr Elf_Half EM_VIDEOCORE = 95;
    constexpr Elf_Half EM_TMM_GPP = 96;
    constexpr Elf_Half EM_NS32K = 97;
    constexpr Elf_Half EM_TPC = 98;
    constexpr Elf_Half EM_SNP1K = 99;
    constexpr Elf_Half EM_ST200 = 100;
    constexpr Elf_Half EM_IP2K = 101;
    constexpr Elf_Half EM_MAX = 102;
    constexpr Elf_Half EM_CR = 103;
    constexpr Elf_Half EM_F2MC16 = 104;
    constexpr Elf_Half EM_MSP430 = 105;
    constexpr Elf_Half EM_BLACKFIN = 106;
    constexpr Elf_Half EM_SE_C33 = 107;
    constexpr Elf_Half EM_SEP = 108;
    constexpr Elf_Half EM_ARCA = 109;
    constexpr Elf_Half EM_UNICORE = 110;
    constexpr Elf_Half EM_EXCESS = 111;
    constexpr Elf_Half EM_DXP = 112;
    constexpr Elf_Half EM_ALTERA_NIOS2 = 113;
    constexpr Elf_Half EM_CRX = 114;
    constexpr Elf_Half EM_XGATE = 115;
    constexpr Elf_Half EM_C166 = 116;
    constexpr Elf_Half EM_M16C = 117;
    constexpr Elf_Half EM_DSPIC30F = 118;
    constexpr Elf_Half EM_CE = 119;
    constexpr Elf_Half EM_M32C = 120;
    constexpr Elf_Half EM_TSK3000 = 131;
    constexpr Elf_Half EM_RS08 = 132;
    constexpr Elf_Half EM_SHARC = 133;
    constexpr Elf_Half EM_ECOG2 = 134;
    constexpr Elf_Half EM_SCORE7 = 135;
    constexpr Elf_Half EM_DSP24 = 136;
    constexpr Elf_Half EM_VIDEOCORE3 = 137;
    constexpr Elf_Half EM_LATTICEMICO32 = 138;
    constexpr Elf_Half EM_SE_C17 = 139;
    constexpr Elf_Half EM_TI_C6000 = 140;
    constexpr Elf_Half EM_TI_C2000 = 141;
    constexpr Elf_Half EM_TI_C5500 = 142;
    constexpr Elf_Half EM_TI_ARP32 = 143;
    constexpr Elf_Half EM_TI_PRU = 144;
    constexpr Elf_Half EM_MMDSP_PLUS = 160;
    constexpr Elf_Half EM_CYPRESS_M8C = 161;
    constexpr Elf_Half EM_R32C = 162;
    constexpr Elf_Half EM_TRIMEDIA = 163;
    constexpr Elf_Half EM_QDSP6 = 164;
    constexpr Elf_Half EM_8051 = 165;
    constexpr Elf_Half EM_STXP7X = 166;
    constexpr Elf_Half EM_NSD32 = 167;
    constexpr Elf_Half EM_ECOG1 = 168;
    constexpr Elf_Half EM_ECOG1X = 168;
    constexpr Elf_Half EM_MAXQ30 = 169;
    constexpr Elf_Half EM_XIMO16 = 170;
    constexpr Elf_Half EM_MANIK = 171;
    constexpr Elf_Half EM_CRAYNV2 = 172;
    constexpr Elf_Half EM_RX = 173;
    constexpr Elf_Half EM_METAG = 174;
    constexpr Elf_Half EM_MCST_ELBRUS = 175;
    constexpr Elf_Half EM_ECOG16 = 176;
    constexpr Elf_Half EM_CR16 = 177;
    constexpr Elf_Half EM_ETPU = 178;
    constexpr Elf_Half EM_SLE9X = 179;
    constexpr Elf_Half EM_L10M = 180;
    constexpr Elf_Half EM_K10M = 181;
    constexpr Elf_Half EM_AARCH64 = 183;
    constexpr Elf_Half EM_AVR32 = 185;
    constexpr Elf_Half EM_STM8 = 186;
    constexpr Elf_Half EM_TILE64 = 187;
    constexpr Elf_Half EM_TILEPRO = 188;
    constexpr Elf_Half EM_MICROBLAZE = 189;
    constexpr Elf_Half EM_CUDA = 190;
    constexpr Elf_Half EM_TILEGX = 191;
    constexpr Elf_Half EM_CLOUDSHIELD = 192;
    constexpr Elf_Half EM_COREA_1ST = 193;
    constexpr Elf_Half EM_COREA_2ND = 194;
    constexpr Elf_Half EM_ARC_COMPACT2 = 195;
    constexpr Elf_Half EM_OPEN8 = 196;
    constexpr Elf_Half EM_RL78 = 197;
    constexpr Elf_Half EM_VIDEOCORE5 = 198;
    constexpr Elf_Half EM_78KOR = 199;
    constexpr Elf_Half EM_56800EX = 200;
    constexpr Elf_Half EM_BA1 = 201;
    constexpr Elf_Half EM_BA2 = 202;
    constexpr Elf_Half EM_XCORE = 203;
    constexpr Elf_Half EM_MCHP_PIC = 204;
    constexpr Elf_Half EM_KM32 = 210;
    constexpr Elf_Half EM_KMX32 = 211;
    constexpr Elf_Half EM_KMX16 = 212;
    constexpr Elf_Half EM_KMX8 = 213;
    constexpr Elf_Half EM_KVARC = 214;
    constexpr Elf_Half EM_CDP = 215;
    constexpr Elf_Half EM_COGE = 216;
    constexpr Elf_Half EM_COOL = 217;
    constexpr Elf_Half EM_NORC = 218;
    constexpr Elf_Half EM_CSR_KALIMBA = 219;
    constexpr Elf_Half EM_Z80 = 220;
    constexpr Elf_Half EM_VISIUM = 221;
    constexpr Elf_Half EM_FT32 = 222;
    constexpr Elf_Half EM_MOXIE = 223;
    constexpr Elf_Half EM_AMDGPU = 224;
    constexpr Elf_Half EM_RISCV = 243;
    constexpr Elf_Half EM_LANAI = 244;
    constexpr Elf_Half EM_CEVA = 245;
    constexpr Elf_Half EM_CEVA_X2 = 246;
    constexpr Elf_Half EM_BPF = 247;
    constexpr Elf_Half EM_GRAPHCORE_IPU = 248;
    constexpr Elf_Half EM_IMG1 = 249;
    constexpr Elf_Half EM_NFP = 250;
    constexpr Elf_Half EM_VE = 251;
    constexpr Elf_Half EM_CSKY = 252;
    constexpr Elf_Half EM_ARC_COMPACT3_64 = 253;
    constexpr Elf_Half EM_MCS6502 = 254;
    constexpr Elf_Half EM_ARC_COMPACT3 = 255;
    constexpr Elf_Half EM_KVX = 256;
    constexpr Elf_Half EM_65816 = 257;
    constexpr Elf_Half EM_LOONGARCH = 258;
    constexpr Elf_Half EM_KF32 = 259;
    constexpr Elf_Half EM_U16_U8CORE = 260;
    constexpr Elf_Half EM_TACHYUM = 261;
    constexpr Elf_Half EM_56800EF = 262;
    constexpr Elf_Half EM_SBF = 263;
    constexpr Elf_Half EM_AIENGINE = 264;
    constexpr Elf_Half EM_SIMA_MLA = 265;
    constexpr Elf_Half EM_BANG = 266;
    constexpr Elf_Half EM_LOONGGPU = 267;
    constexpr Elf_Half EM_SW64 = 268;
    constexpr Elf_Half EM_AIECTRLCODE = 269;

    constexpr Elf_Word EV_NONE = 0;
    constexpr Elf_Word EV_CURRENT = 1;

    constexpr Elf_Half SHN_UNDEF = 0;
    constexpr Elf_Half SHN_LORESERVE = 0xFF00;
    constexpr Elf_Half SHN_LOPROC = 0xFF00;
    constexpr Elf_Half SHN_HIPROC = 0xFF1F;
    constexpr Elf_Half SHN_LOOS = 0xFF20;
    constexpr Elf_Half SHN_HIOS = 0xFF3F;
    constexpr Elf_Half SHN_ABS = 0xFFF1;
    constexpr Elf_Half SHN_COMMON = 0xFFF2;
    constexpr Elf_Half SHN_HIRESERVE = 0xFFFF;

    struct Elf32_Shdr
    {
        Elf_Word sh_name;
        Elf_Word sh_type;
        Elf_Word sh_flags;
        Elf_Addr32 sh_addr;
        Elf_Off32 sh_offset;
        Elf_Word sh_size;
        Elf_Word sh_link;
        Elf_Word sh_info;
        Elf_Word sh_addralign;
        Elf_Word sh_entsize;
    };

    struct Elf64_Shdr
    {
        Elf_Word sh_name;
        Elf_Word sh_type;
        Elf_Xword sh_flags;
        Elf_Addr64 sh_addr;
        Elf_Off64 sh_offset;
        Elf_Xword sh_size;
        Elf_Word sh_link;
        Elf_Word sh_info;
        Elf_Xword sh_addralign;
        Elf_Xword sh_entsize;
    };

    constexpr Elf_Word SHT_NULL = 0;
    constexpr Elf_Word SHT_PROGBITS = 1;
    constexpr Elf_Word SHT_SYMTAB = 2;
    constexpr Elf_Word SHT_STRTAB = 3;
    constexpr Elf_Word SHT_RELA = 4;
    constexpr Elf_Word SHT_HASH = 5;
    constexpr Elf_Word SHT_DYNAMIC = 6;
    constexpr Elf_Word SHT_NOTE = 7;
    constexpr Elf_Word SHT_NOBITS = 8;
    constexpr Elf_Word SHT_REL = 9;
    constexpr Elf_Word SHT_SHLIB = 10;
    constexpr Elf_Word SHT_DYNSYM = 11;
    constexpr Elf_Word SHT_INIT_ARRAY = 14;
    constexpr Elf_Word SHT_FINI_ARRAY = 15;
    constexpr Elf_Word SHT_PREINIT_ARRAY = 16;
    constexpr Elf_Word SHT_GROUP = 17;
    constexpr Elf_Word SHT_SYMTAB_SHNDX = 18;
    constexpr Elf_Word SHT_RELR = 19;
    constexpr Elf_Word SHT_LOOS = 0x6000'0000;
    constexpr Elf_Word SHT_HIOS = 0x6FFF'FFFF;
    constexpr Elf_Word SHT_LOPROC = 0x7000'0000;
    constexpr Elf_Word SHT_HIPROC = 0x7FFF'FFFF;
    constexpr Elf_Word SHT_LOUSER = 0x8000'0000;
    constexpr Elf_Word SHT_HIUSER = 0xFFFF'FFFF;

    constexpr Elf_Word SHF_WRITE = 0x1;
    constexpr Elf_Word SHF_ALLOC = 0x2;
    constexpr Elf_Word SHF_EXECINSTR = 0x4;
    constexpr Elf_Word SHF_MERGE = 0x10;
    constexpr Elf_Word SHF_STRINGS = 0x20;
    constexpr Elf_Word SHF_INFO_LINK = 0x40;
    constexpr Elf_Word SHF_LINK_ORDER = 0x80;
    constexpr Elf_Word SHF_OS_NONCONFORMING = 0x100;
    constexpr Elf_Word SHF_GROUP  = 0x200;
    constexpr Elf_Word SHF_TLS = 0x400;
    constexpr Elf_Word SHF_COMPRESSED = 0x800;
    constexpr Elf_Xword SHF_MASKOS = 0x0F00'0000;
    constexpr Elf_Word SHF_MASKPROC = 0xF000'0000;

    struct Elf32_Sym
    {
        Elf_Word st_name;
        Elf_Addr32 st_value;
        Elf_Word st_size;
        Elf_Char st_info;
        Elf_Char st_other;
        Elf_Half st_shndx;
    };

    struct Elf64_Sym
    {
        Elf_Word st_name;
        Elf_Char st_info;
        Elf_Char st_other;
        Elf_Half st_shndx;
        Elf_Addr64 st_value;
        Elf_Xword st_size;
    };

#define ELF_ST_BIND(i) ((i) >> 4)
#define ELF_ST_TYPE(i) ((i) & 0xF)
#define ELF_ST_INFO (b, t) (((b) << 4) + ((t) & 0xF))
#define ELF_ST_VISIBILITY(o) ((o) & 0x7)

    constexpr Elf_Char STB_LOCAL = 0;
    constexpr Elf_Char STB_GLOBAL = 1;
    constexpr Elf_Char STB_WEAK = 2;
    constexpr Elf_Char STB_LOOS = 10;
    constexpr Elf_Char STB_HIOS = 12;
    constexpr Elf_Char STB_LOPROC = 13;
    constexpr Elf_Char STB_HIPROC = 15;

    constexpr Elf_Char STT_NOTYPE = 0;
    constexpr Elf_Char STT_OBJECT = 1;
    constexpr Elf_Char STT_FUNC = 2;
    constexpr Elf_Char STT_SECTION = 3;
    constexpr Elf_Char STT_FILE = 4;
    constexpr Elf_Char STT_COMMON = 5;
    constexpr Elf_Char STT_TLS = 6;
    constexpr Elf_Char STT_LOOS = 10;
    constexpr Elf_Char STT_HIOS = 12;
    constexpr Elf_Char STT_LOPROC = 13;
    constexpr Elf_Char STT_HIPROC = 15;

    constexpr Elf_Char STV_DEFAULT = 0;
    constexpr Elf_Char STV_INTERNAL = 1;
    constexpr Elf_Char STV_HIDDEN = 2;
    constexpr Elf_Char STV_PROTECTED = 3;
    constexpr Elf_Char STV_EXPORTED = 4;
    constexpr Elf_Char STV_SINGLETON = 5;
    constexpr Elf_Char STV_ELIMATE = 6;

    struct Elf32_Rel
    {
        Elf_Addr32 r_offset;
        Elf_Word r_info;
    };

    struct Elf64_Rel
    {
        Elf_Addr64 r_offset;
        Elf_Xword r_info;
    };

    struct Elf32_Rela
    {
        Elf_Addr32 r_offset;
        Elf_Word r_info;
        Elf_Sword r_addend;
    };


    struct Elf64_Rela
    {
        Elf_Addr64 r_offset;
        Elf_Xword r_info;
        Elf_Sxword r_addend;
    };

#define ELF32_R_SYM(i) ((i) >> 8)
#define ELF32_R_TYPE(i) ((unsigned char)(i))
#define ELF32_R_INFO(s, t) (((s) << 8) + (unsigned char)(t))
#define ELF64_R_SYM(i) ((i) >> 32)
#define ELF64_R_TYPE(i) ((i) & 0xFFFF'FFFFl)
#define ELF64_R_INFO(s, t) ((s) << 32 + ((t) 0xFFFF'FFFFl))

    using Elf32_Relr = Elf_Word;
    using Elf64_Relr = Elf_Xword;

    constexpr Elf_Word R_68K_NONE = 0;
    constexpr Elf_Word R_68K_32 = 1;
    constexpr Elf_Word R_68K_16 = 2;
    constexpr Elf_Word R_68K_8 = 3;
    constexpr Elf_Word R_68K_PC32 = 4;
    constexpr Elf_Word R_68K_PC16 = 5;
    constexpr Elf_Word R_68K_PC8 = 6;
    constexpr Elf_Word R_68K_GOT32 = 7;
    constexpr Elf_Word R_68K_GOT16 = 8;
    constexpr Elf_Word R_68K_GOT8 = 9;
    constexpr Elf_Word R_68K_GOT32O = 10;
    constexpr Elf_Word R_68K_GOT16O = 11;
    constexpr Elf_Word R_68K_GOT8O = 12;
    constexpr Elf_Word R_68K_PLT32 = 13;
    constexpr Elf_Word R_68K_PLT16 = 14;
    constexpr Elf_Word R_68K_PLT8 = 15;
    constexpr Elf_Word R_68K_PLT32O = 16;
    constexpr Elf_Word R_68K_PLT16O = 17;
    constexpr Elf_Word R_68K_PLT8O = 18;
    constexpr Elf_Word R_68K_COPY = 19;
    constexpr Elf_Word R_68K_GLOB_DAT = 20;
    constexpr Elf_Word R_68K_JMP_SLOT = 21;
    constexpr Elf_Word R_68K_RELATIVE = 22;

    constexpr Elf_Word R_X86_64_NONE = 0;
    constexpr Elf_Word R_X86_64_64 = 1;
    constexpr Elf_Word R_X86_64_PC32 = 2;
    constexpr Elf_Word R_X86_64_GOT32 = 3;
    constexpr Elf_Word R_X86_64_PLT32 = 4;
    constexpr Elf_Word R_X86_64_COPY = 5;
    constexpr Elf_Word R_X86_64_GLOB_DAT = 6;
    constexpr Elf_Word R_X86_64_JUMP_SLOT = 7;
    constexpr Elf_Word R_X86_64_RELATIVE = 8;
    constexpr Elf_Word R_X86_64_GOTPCREL = 9;
    constexpr Elf_Word R_X86_64_32 = 10;
    constexpr Elf_Word R_X86_64_32S = 11;
    constexpr Elf_Word R_X86_64_16 = 12;
    constexpr Elf_Word R_X86_64_PC16 = 13;
    constexpr Elf_Word R_X86_64_8 = 14;
    constexpr Elf_Word R_X86_64_PC8 = 15;
    constexpr Elf_Word R_X86_64_DTPMOD64 = 16;
    constexpr Elf_Word R_X86_64_DTPOFF64 = 17;
    constexpr Elf_Word R_X86_64_TPOFF64 = 18;
    constexpr Elf_Word R_X86_64_TLSGD = 19;
    constexpr Elf_Word R_X86_64_TLSLD = 20;
    constexpr Elf_Word R_X86_64_DTPOFF32 = 21;
    constexpr Elf_Word R_X86_64_GOTTPOFF = 22;
    constexpr Elf_Word R_X86_64_TPOFF32 = 23;
    constexpr Elf_Word R_X86_64_PC64 = 24;
    constexpr Elf_Word R_X86_64_GOTOFF64 = 25;
    constexpr Elf_Word R_X86_64_GOTPC32 = 26;
    constexpr Elf_Word R_X86_64_GOT64 = 27;
    constexpr Elf_Word R_X86_64_GOTPCREL64 = 28;
    constexpr Elf_Word R_X86_64_GOTPC64 = 29;
    constexpr Elf_Word R_X86_64_PLTOFF64 = 31;
    constexpr Elf_Word R_X86_64_SIZE32 = 32;
    constexpr Elf_Word R_X86_64_SIZE64 = 33;
    constexpr Elf_Word R_X86_64_GOTPC32_TLSDESC = 34;
    constexpr Elf_Word R_X86_64_TLSDESC_CALL = 35;
    constexpr Elf_Word R_X86_64_TLSDESC = 36;
    constexpr Elf_Word R_X86_64_IRELATIVE = 37;
    constexpr Elf_Word R_X86_64_RELATIVE64 = 38;
    constexpr Elf_Word R_X86_64_GOTPCRELX = 41;
    constexpr Elf_Word R_X86_64_REX_GOTPCRELX = 42;

    constexpr Elf_Word R_RISCV_NONE = 0;
    constexpr Elf_Word R_RISCV_32 = 1;
    constexpr Elf_Word R_RISCV_64 = 2;
    constexpr Elf_Word R_RISCV_RELATIVE = 3;
    constexpr Elf_Word R_RISCV_COPY = 4;
    constexpr Elf_Word R_RISCV_JUMP_SLOT = 5;
    constexpr Elf_Word R_RISCV_TLS_DTPMOD32 = 6;
    constexpr Elf_Word R_RISCV_TLS_DTPMOD64 = 7;
    constexpr Elf_Word R_RISCV_TLS_DTPREL32 = 8;
    constexpr Elf_Word R_RISCV_TLS_DTPREL64 = 9;
    constexpr Elf_Word R_RISCV_TLS_TPREL32 = 10;
    constexpr Elf_Word R_RISCV_TLS_TPREL64 = 11;
    constexpr Elf_Word R_RISCV_BRANCH = 16;
    constexpr Elf_Word R_RISCV_JAL = 17;
    constexpr Elf_Word R_RISCV_CALL = 18;
    constexpr Elf_Word R_RISCV_CALL_PLT = 19;
    constexpr Elf_Word R_RISCV_GOT_HI20 = 20;
    constexpr Elf_Word R_RISCV_TLS_GOT_HI20 = 21;
    constexpr Elf_Word R_RISCV_TLS_GD_HI20 = 22;
    constexpr Elf_Word R_RISCV_PCREL_HI20 = 24;
    constexpr Elf_Word R_RISCV_PCREL_LO12_I = 24;
    constexpr Elf_Word R_RISCV_PCREL_LO12_S = 25;
    constexpr Elf_Word R_RISCV_HI20 = 26;
    constexpr Elf_Word R_RISCV_LO12_I = 27;
    constexpr Elf_Word R_RISCV_LO12_S = 28;
    constexpr Elf_Word R_RISCV_TPREL_HI20 = 29;
    constexpr Elf_Word R_RISCV_TPREL_LO12_I = 30;
    constexpr Elf_Word R_RISCV_TPREL_LO12_S = 31;
    constexpr Elf_Word R_RISCV_TPREL_ADD = 32;
    constexpr Elf_Word R_RISCV_ADD8 = 33;
    constexpr Elf_Word R_RISCV_ADD16 = 34;
    constexpr Elf_Word R_RISCV_ADD32 = 35;
    constexpr Elf_Word R_RISCV_ADD64 = 36;
    constexpr Elf_Word R_RISCV_SUB8 = 37;
    constexpr Elf_Word R_RISCV_SUB16 = 38;
    constexpr Elf_Word R_RISCV_SUB32 = 39;
    constexpr Elf_Word R_RISCV_SUB64 = 40;
    constexpr Elf_Word R_RISCV_ALIGN = 43;
    constexpr Elf_Word R_RISCV_RVC_BRANCH = 44;
    constexpr Elf_Word R_RISCV_RVC_JUMP = 45;
    constexpr Elf_Word R_RISCV_RVC_LUI = 46;
    constexpr Elf_Word R_RISCV_RELAX = 51;
    constexpr Elf_Word R_RISCV_SUB6 = 52;
    constexpr Elf_Word R_RISCV_SET6 = 53;
    constexpr Elf_Word R_RISCV_SET8 = 54;
    constexpr Elf_Word R_RISCV_SET16 = 55;
    constexpr Elf_Word R_RISCV_SET32 = 56;
    constexpr Elf_Word R_RISCV_32_PCREL = 57;
    constexpr Elf_Word R_RISCV_IRELATIVE = 58;

    struct Elf32_Phdr
    {
        Elf_Word p_type;
        Elf_Off32 p_offset;
        Elf_Addr32 p_vaddr;
        Elf_Addr32 p_paddr;
        Elf_Word p_filesz;
        Elf_Word p_memsz;
        Elf_Word p_flags;
        Elf_Word p_align;
    };

    struct Elf64_Phdr
    {
        Elf_Word p_type;
        Elf_Word p_flags;
        Elf_Off64 p_offset;
        Elf_Addr64 p_vaddr;
        Elf_Addr64 p_paddr;
        Elf_Xword p_filesz;
        Elf_Xword p_memsz;
        Elf_Xword p_align;
    };

    constexpr Elf_Word PT_NULL = 0;
    constexpr Elf_Word PT_LOAD = 1;
    constexpr Elf_Word PT_DYNAMIC = 2;
    constexpr Elf_Word PT_INTERP = 3;
    constexpr Elf_Word PT_NOTE = 4;
    constexpr Elf_Word PT_SHLIB = 5;
    constexpr Elf_Word PT_PHDR = 6;
    constexpr Elf_Word PT_TLS = 7;
    constexpr Elf_Word PT_LOOS = 0x6000'0000;
    constexpr Elf_Word PT_HIOS = 0x6FFF'FFFF;
    constexpr Elf_Word PT_LOPROC = 0x7000'0000;
    constexpr Elf_Word PT_HIPROC = 0x7FFF'FFFF;

    constexpr Elf_Word PF_X = 0x1;
    constexpr Elf_Word PF_W = 0x2;
    constexpr Elf_Word PF_R = 0x4;
    constexpr Elf_Word PF_MASKOS = 0x00FF'0000;
    constexpr Elf_Word PF_MASKPROC = 0xFF00'0000;

    struct Elf32_Dyn
    {
        Elf_Sword d_tag;
        union
        {
            Elf_Word d_val;
            Elf_Addr32 d_ptr;
        };
    };

    struct Elf64_Dyn
    {
        Elf_Sxword d_tag;

        union
        {
            Elf_Xword d_val;
            Elf_Addr64 d_ptr;
        };
    };

    constexpr Elf_Sword DT_NULL = 0;
    constexpr Elf_Sword DT_NEEDED = 1;
    constexpr Elf_Sword DT_PLTRELSZ = 2;
    constexpr Elf_Sword DT_PLTGOT = 3;
    constexpr Elf_Sword DT_HASH = 4;
    constexpr Elf_Sword DT_STRTAB = 5;
    constexpr Elf_Sword DT_SYMTAB = 6;
    constexpr Elf_Sword DT_RELA = 7;
    constexpr Elf_Sword DT_RELASZ = 8;
    constexpr Elf_Sword DT_RELAENT = 9;
    constexpr Elf_Sword DT_STRSZ = 10;
    constexpr Elf_Sword DT_SYMENT = 11;
    constexpr Elf_Sword DT_INIT = 12;
    constexpr Elf_Sword DT_FINI = 13;
    constexpr Elf_Sword DT_SONAME = 14;
    constexpr Elf_Sword DT_RPATH = 15;
    constexpr Elf_Sword DT_SYMBOLIC = 16;
    constexpr Elf_Sword DT_REL = 17;
    constexpr Elf_Sword DT_RELSZ = 18;
    constexpr Elf_Sword DT_RELENT = 19;
    constexpr Elf_Sword DT_PLTREL = 20;
    constexpr Elf_Sword DT_DEBUG = 21;
    constexpr Elf_Sword DT_TEXTREL = 22;
    constexpr Elf_Sword DT_JMPREL = 23;
    constexpr Elf_Sword DT_BIND_NOW = 24;
    constexpr Elf_Sword DT_INIT_ARRAY = 25;
    constexpr Elf_Sword DT_FINI_ARRAY = 26;
    constexpr Elf_Sword DT_INIT_ARRAYSZ = 27;
    constexpr Elf_Sword DT_FINI_ARRAYSZ = 28;
    constexpr Elf_Sword DT_RUNPATH = 29;
    constexpr Elf_Sword DT_FLAGS = 30;
    constexpr Elf_Sword DT_ENCODING = 31;
    constexpr Elf_Sword DT_PREINIT_ARRAY = 32;
    constexpr Elf_Sword DT_PREINIT_ARRAYSZ = 33;
    constexpr Elf_Sword DT_SYMTAB_SHNDX = 34;
    constexpr Elf_Sword DT_RELRSZ = 35;
    constexpr Elf_Sword DT_RELR = 36;
    constexpr Elf_Sword DT_RELRENT = 37;
    constexpr Elf_Sword DT_SYMTABSZ = 39;
    constexpr Elf_Sword DT_LOOS = 0x6000'0000;
    constexpr Elf_Sword DT_HIOS = 0x6FFF'FFFF0;
    constexpr Elf_Sword DT_LPROC = 0x7000'0000;
    constexpr Elf_Sword DT_HPROC = 0x7FFF'FFFF;
}
