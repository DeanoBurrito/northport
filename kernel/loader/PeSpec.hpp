#pragma once

#include <Types.hpp>

namespace Npk
{
    struct DOS_HEADER
    {
        uint16_t e_magic;
        uint16_t e_cblp;
        uint16_t e_cp;
        uint16_t e_crlc;
        uint16_t e_cparhdr;
        uint16_t e_minalloc;
        uint16_t e_maxalloc;
        uint16_t e_ss;
        uint16_t e_sp;
        uint16_t e_csum;
        uint16_t e_ip;
        uint16_t e_cs;
        uint16_t e_lfarlc;
        uint16_t e_ovno;
        uint16_t e_res[4];
        uint16_t e_oemid;
        uint16_t e_oeminfo;
        uint16_t e_res2[10];
        uint32_t e_lfanew;
    };

    constexpr uint16_t DOS_MAGIC = 0x5A4D;

    struct IMAGE_FILE_HEADER
    {
        uint16_t Machine;
        uint16_t NumberOfSections;
        uint32_t TimeDateStamp;
        uint32_t PointerToSymbolTable;
        uint32_t NumberOfSymbols;
        uint16_t SizeOfOptionalHeader;
        uint16_t Characteristics;
    };

    constexpr uint16_t IMAGE_FILE_MACHINE_UNKNOWN = 0x0;
    constexpr uint16_t IMAGE_FILE_MACHINE_ALPHA = 0x184;
    constexpr uint16_t IMAGE_FILE_MACHINE_ALPHA64 = 0x284;
    constexpr uint16_t IMAGE_FILE_MACHINE_AM33 = 0x1D3;
    constexpr uint16_t IMAGE_FILE_MACHINE_AMD64 = 0x8664;
    constexpr uint16_t IMAGE_FILE_MACHINE_ARM = 0x1C0;
    constexpr uint16_t IMAGE_FILE_MACHINE_ARM64 = 0xAA64;
    constexpr uint16_t IMAGE_FILE_MACHINE_ARM64EC = 0xA641;
    constexpr uint16_t IMAGE_FILE_MACHINE_ARM64X = 0xA64E;
    constexpr uint16_t IMAGE_FILE_MACHINE_ARMNT = 0x1C4;
    constexpr uint16_t IMAGE_FILE_MACHINE_AXP64 = 0x284;
    constexpr uint16_t IMAGE_FILE_MACHINE_EBC = 0xEBC;
    constexpr uint16_t IMAGE_FILE_MACHINE_I386 = 0x14C;
    constexpr uint16_t IMAGE_FILE_MACHINE_IA64 = 0x200;
    constexpr uint16_t IMAGE_FILE_MACHINE_LOONGARCH32 = 0x6232;
    constexpr uint16_t IMAGE_FILE_MACHINE_LOONGARCH64 = 0x6264;
    constexpr uint16_t IMAGE_FILE_MACHINE_M32R = 0x9041;
    constexpr uint16_t IMAGE_FILE_MACHINE_MIPS16 = 0x266;
    constexpr uint16_t IMAGE_FILE_MACHINE_MIPSFPU = 0x366;
    constexpr uint16_t IMAGE_FILE_MACHINE_MIPSFPU16 = 0x466;
    constexpr uint16_t IMAGE_FILE_MACHINE_POWERPC = 0x1F0;
    constexpr uint16_t IMAGE_FILE_MACHINE_POWERPCFP = 0x1F1;
    constexpr uint16_t IMAGE_FILE_MACHINE_R3000BE = 0x160;
    constexpr uint16_t IMAGE_FILE_MACHINE_R3000 = 0x162;
    constexpr uint16_t IMAGE_FILE_MACHINE_R4000 = 0x166;
    constexpr uint16_t IMAGE_FILE_MACHINE_R10000 = 0x168;
    constexpr uint16_t IMAGE_FILE_MACHINE_RISCV32 = 0x5032;
    constexpr uint16_t IMAGE_FILE_MACHINE_RISCV64 = 0x5064;
    constexpr uint16_t IMAGE_FILE_MACHINE_RISCV128 = 0x5128;
    constexpr uint16_t IMAGE_FILE_MACHINE_SH3 = 0x1A2;
    constexpr uint16_t IMAGE_FILE_MACHINE_SH3DSP = 0x1A3;
    constexpr uint16_t IMAGE_FILE_MACHINE_SH4 = 0x1A4;
    constexpr uint16_t IMAGE_FILE_MACHINE_SH5 = 0x1A8;
    constexpr uint16_t IMAGE_FILE_MACHINE_THUMB = 0x1C2;
    constexpr uint16_t IMAGE_FILE_MACHINE_WCEMIPSV2 = 0x169;

    constexpr uint16_t IMAGE_FILE_RELOCS_STRIPPED = 0x0001;
    constexpr uint16_t IMAGE_FILE_EXECUTABLE_IMAGE = 0x0002;
    constexpr uint16_t IMAGE_FILE_LINE_NUMS_STRIPPED = 0x0004;
    constexpr uint16_t IMAGE_FILE_LOCAL_SYMS_STRIPPED = 0x0008;
    constexpr uint16_t IMAGE_FILE_AGGRESSIVE_WS_TRIM = 0x0010;
    constexpr uint16_t IMAGE_FILE_LARGE_ADDRESS_AWARE = 0x0020;
    constexpr uint16_t IMAGE_FILE_BYTES_REVERSED_LO = 0x0080;
    constexpr uint16_t IMAGE_FILE_32BIT_MACHINE = 0x0100;
    constexpr uint16_t IMAGE_FILE_DEBUG_STRIPPED = 0x200;
    constexpr uint16_t IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP = 0x0400;
    constexpr uint16_t IMAGE_FILE_NET_RUN_FROM_SWAP = 0x0800;
    constexpr uint16_t IMAGE_FILE_SYSTEM = 0x1000;
    constexpr uint16_t IMAGE_FILE_DLL = 0x2000;
    constexpr uint16_t IMAGE_FILE_UP_SYSTEM_ONLY = 0x4000;
    constexpr uint16_t IMAGE_FILE_BYTES_REVERSED_HI = 0x8000;

    struct IMAGE_DATA_DIRECTORY
    {
        uint32_t VirtualAddress;
        uint32_t Size;
    };

    struct IMAGE_OPTIONAL_HEADER32
    {
        uint16_t Magic;
        uint8_t MajorLinkerVersion;
        uint8_t MinorLinkerVersion;
        uint32_t SizeOfCode;
        uint32_t SizeOfInitializedData;
        uint32_t SizeOfUninitializedData;
        uint32_t AddressOfEntryPoint;
        uint32_t BaseOfCode;
        uint32_t BaseOfData;
        uint32_t ImageBase;
        uint32_t SectionAlignment;
        uint32_t FileAlignment;
        uint16_t MajorOperatingSystemVersion;
        uint16_t MinorOperatingSystemVersion;
        uint16_t MajorImageVersion;
        uint16_t MinorImageVersion;
        uint16_t MajorSubsystemVersion;
        uint16_t MinorSubsystemVersion;
        uint32_t Win32VersionValue;
        uint32_t SizeOfImage;
        uint32_t SizeOfHeaders;
        uint32_t CheckSum;
        uint16_t Subsystem;
        uint16_t DllCharacteristics;
        uint32_t SizeOfStackReserve;
        uint32_t SizeOfStackCommit;
        uint32_t SizeOfHeapReserve;
        uint32_t SizeOfHeapCommit;
        uint32_t LoaderFlags;
        uint32_t NumberOfRvaAndSizes;
        IMAGE_DATA_DIRECTORY DataDirectories[];
    };

    struct IMAGE_OPTIONAL_HEADER64
    {
        uint16_t Magic;
        uint8_t MajorLinkerVersion;
        uint8_t MinorLinkerVersion;
        uint32_t SizeOfCode;
        uint32_t SizeOfInitializedData;
        uint32_t SizeOfUninitializedData;
        uint32_t AddressOfEntryPoint;
        uint32_t BaseOfCode;
        uint64_t ImageBase;
        uint32_t SectionAlignment;
        uint32_t FileAlignment;
        uint16_t MajorOperatingSystemVersion;
        uint16_t MinorOperatingSystemVersion;
        uint16_t MajorImageVersion;
        uint16_t MinorImageVersion;
        uint16_t MajorSubsystemVersion;
        uint16_t MinorSubsystemVersion;
        uint32_t Win32VersionValue;
        uint32_t SizeOfImage;
        uint32_t SizeOfHeaders;
        uint32_t CheckSum;
        uint16_t Subsystem;
        uint16_t DllCharacteristics;
        uint64_t SizeOfStackReserve;
        uint64_t SizeOfStackCommit;
        uint64_t SizeOfHeapReserve;
        uint64_t SizeOfHeapCommit;
        uint32_t LoaderFlags;
        uint32_t NumberOfRvaAndSizes;
        IMAGE_DATA_DIRECTORY DataDirectories[];
    };

    constexpr uint16_t IMAGE_MAGIC_PE32 = 0x10B;
    constexpr uint16_t IMAGE_MAGIC_ROM = 0x107;
    constexpr uint16_t IMAGE_MAGIC_PE32PLUS = 0x20B;

    constexpr uint16_t IMAGE_SUBSYSTEM_UNKNOWN = 0;
    constexpr uint16_t IMAGE_SUBSYSTEM_NATIVE = 1;
    constexpr uint16_t IMAGE_SUBSYSTEM_WINDOWS_GUI = 2;
    constexpr uint16_t IMAGE_SUBSYSTEM_WINDOWS_CUI = 3;
    constexpr uint16_t IMAGE_SUBSYSTEM_OS2_CUI = 5;
    constexpr uint16_t IMAGE_SUBSYSTEM_POSIX_CUI = 7;
    constexpr uint16_t IMAGE_SUBSYSTEM_NATIVE_WINDOWS = 8;
    constexpr uint16_t IMAGE_SUBSYSTEM_WINDOWS_CE_GUI = 9;
    constexpr uint16_t IMAGE_SUBSYSTEM_EFI_APPLICATION = 10;
    constexpr uint16_t IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER = 11;
    constexpr uint16_t IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER = 12;
    constexpr uint16_t IMAGE_SUBSYSTEM_EFI_ROM = 13;
    constexpr uint16_t IMAGE_SUBSYSTEM_XBOX = 14;
    constexpr uint16_t IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION = 16;

    constexpr uint16_t IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA = 0x0020;
    constexpr uint16_t IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE = 0x0040;
    constexpr uint16_t IMAGE_DLLCHARACTERISTICS_NX_COMPAT = 0x0100;
    constexpr uint16_t IMAGE_DLLCHARACTERISTICS_NO_ISOLATION = 0x200;
    constexpr uint16_t IMAGE_DLLCHARACTERISTICS_NO_SEH = 0x0400;
    constexpr uint16_t IMAGE_DLLCHARACTERISTICS_NO_BIND = 0x0800;
    constexpr uint16_t IMAGE_DLLCHARACTERISTICS_APPCONTAINER = 0x1000;
    constexpr uint16_t IMAGE_DLLCHARACTERISTICS_WDM_DRIVER = 0x2000;
    constexpr uint16_t IMAGE_DLLCHARACTERISTICS_GUARD_CF = 0x4000;
    constexpr uint16_t IMAGE_DLLCHARACTERISTICS_TERMINAL_SERVER_AWARE = 0x8000;

    constexpr size_t IMAGE_DIRECTORY_ENTRY_EXPORT = 0;
    constexpr size_t IMAGE_DIRECTORY_ENTRY_IMPORT = 1;
    constexpr size_t IMAGE_DIRECTORY_ENTRY_RESOURCE = 2;
    constexpr size_t IMAGE_DIRECTORY_ENTRY_EXCEPTION = 3;
    constexpr size_t IMAGE_DIRECTORY_ENTRY_SECURITY = 4;
    constexpr size_t IMAGE_DIRECTORY_ENTRY_BASERELOC = 5;
    constexpr size_t IMAGE_DIRECTORY_ENTRY_DEBUG = 6;
    constexpr size_t IMAGE_DIRECTORY_ENTRY_ARCHITECTURE = 7;
    constexpr size_t IMAGE_DIRECTORY_ENTRY_GLOBALPTR = 8;
    constexpr size_t IMAGE_DIRECTORY_ENTRY_TLS = 9;
    constexpr size_t IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG = 10;
    constexpr size_t IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT = 11;
    constexpr size_t IMAGE_DIRECTORY_ENTRY_IAT = 12;
    constexpr size_t IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT = 13;
    constexpr size_t IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR = 14;

    struct IMAGE_SECTION_HEADER
    {
        char Name[8];
        uint32_t VirtualSize;
        uint32_t VirtualAddress;
        uint32_t SizeOfRawData;
        uint32_t PointerToRawData;
        uint32_t PointerToRelocations;
        uint32_t PointerToLinenumbers;
        uint16_t NumberOfRelocations;
        uint16_t NumberOfLinenumbers;
        uint32_t Characteristics;
    };

    constexpr uint32_t IMAGE_SCN_TYPE_NO_PAD = 0x0000'0008;
    constexpr uint32_t IMAGE_SCN_CNT_CODE = 0x0000'0020;
    constexpr uint32_t IMAGE_SCN_CNT_INITIALIZED_DATA = 0x0000'00040;
    constexpr uint32_t IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x0000'0080;
    constexpr uint32_t IMAGE_SCN_LNK_OTHER = 0x0000'0100;
    constexpr uint32_t IMAGE_SCN_LNK_INFO = 0x0000'0200;
    constexpr uint32_t IMAGE_SCN_LNK_REMOVE = 0x0000'0800;
    constexpr uint32_t IMAGE_SCN_LNK_COMDAT = 0x0000'1000;
    constexpr uint32_t IMAGE_SCN_GPREL = 0x0000'8000;
    constexpr uint32_t IMAGE_SCN_MEM_PURGEABLE = 0x0002'0000;
    constexpr uint32_t IMAGE_SCN_MEM_16BIT = 0x0002'0000;
    constexpr uint32_t IMAGE_SCN_MEM_LOCKED = 0x0004'0000;
    constexpr uint32_t IMAGE_SCN_MEM_PRELOAD = 0x0008'0000;
    constexpr uint32_t IMAGE_SCN_ALIGN_1BYTES = 0x0010'0000;
    constexpr uint32_t IMAGE_SCN_ALIGN_2BYTES = 0x0020'0000;
    constexpr uint32_t IMAGE_SCN_ALIGN_4BYTES = 0x0030'0000;
    constexpr uint32_t IMAGE_SCN_ALIGN_8BYTES = 0x0040'0000;
    constexpr uint32_t IMAGE_SCN_ALIGN_16BYTES = 0x0050'0000;
    constexpr uint32_t IMAGE_SCN_ALIGN_32BYTES = 0x0060'0000;
    constexpr uint32_t IMAGE_SCN_ALIGN_64BYTES = 0x0070'0000;
    constexpr uint32_t IMAGE_SCN_ALIGN_128BYTES = 0x0080'0000;
    constexpr uint32_t IMAGE_SCN_ALIGN_256BYTES = 0x0090'0000;
    constexpr uint32_t IMAGE_SCN_ALIGN_512BYTES = 0x00A0'0000;
    constexpr uint32_t IMAGE_SCN_ALIGN_1024BYTES = 0x00B0'0000;
    constexpr uint32_t IMAGE_SCN_ALIGN_2048BYTES = 0x00C0'0000;
    constexpr uint32_t IMAGE_SCN_ALIGN_4096BYTES = 0x00D0'0000;
    constexpr uint32_t IMAGE_SCN_ALIGN_8192BYTES = 0x00E0'0000;
    constexpr uint32_t IMAGE_SCN_LNK_NRELOC_OVFL = 0x0100'0000;
    constexpr uint32_t IMAGE_SCN_MEM_DISCARDABLE = 0x0200'0000;
    constexpr uint32_t IMAGE_SCN_MEM_NOT_CACHED = 0x0400'0000;
    constexpr uint32_t IMAGE_SCN_MEM_NOT_PAGED = 0x0800'0000;
    constexpr uint32_t IMAGE_SCN_MEM_SHARED = 0x1000'0000;
    constexpr uint32_t IMAGE_SCN_MEM_EXECUTE = 0x2000'0000;
    constexpr uint32_t IMAGE_SCN_MEM_READ = 0x4000'0000;
    constexpr uint32_t IMAGE_SCN_MEM_WRITE = 0x8000'0000;
}
