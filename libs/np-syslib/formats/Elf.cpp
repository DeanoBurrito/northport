#include <formats/Elf.h>
#include <NativePtr.h>

namespace sl
{
    bool ValidateElfHeader(const void* file, Elf_Half type)
    {
#ifdef __x86_64__
        constexpr Elf64_UnsignedChar elfClass = ELFCLASS64;
        constexpr Elf64_UnsignedChar elfData = ELFDATA2LSB;
        constexpr Elf64_Half elfMach = EM_X86_64;
#elif __riscv_xlen == 64
        constexpr Elf64_UnsignedChar elfClass = ELFCLASS64;
        constexpr Elf64_UnsignedChar elfData = ELFDATA2LSB;
        constexpr Elf64_Half elfMach = EM_RISCV;
#elif __m68k__
        constexpr Elf32_UnsignedChar elfClass = ELFCLASS32;
        constexpr Elf32_UnsignedChar elfData = ELFDATA2MSB;
        constexpr Elf32_Half elfMach = EM_68K;
#else
    #error "syslib/Elf.cpp: Unknown architecture"
#endif

        auto hdr = reinterpret_cast<const Elf_Ehdr*>(file);
        if (memcmp(hdr->e_ident, ExpectedMagic, 4) != 0)
            return false;

        if(hdr->e_ident[EI_CLASS] != elfClass)
            return false;
        if (hdr->e_ident[EI_DATA] != elfData)
            return false;
        if (hdr->e_machine != elfMach)
            return false;
        if (hdr->e_version != EV_CURRENT)
            return false;
        if (hdr->e_type != type)
            return false;

        return true;
    }

    ComputedReloc ComputeRelocation(Elf_Word type, uintptr_t a, uintptr_t b, uintptr_t s)
    {
        switch (type)
        {
#ifdef __x86_64__
        case R_X86_64_64: return { .value = a + s, .length = 8, .usedSymbol = true };
        case R_X86_64_32: return { .value = a + s, .length = 4, .usedSymbol = true };
        case R_X86_64_RELATIVE: return { .value = b + a, .length = sizeof(void*), .usedSymbol = false };
        case R_X86_64_JUMP_SLOT: return { .value = s, .length = sizeof(void*), .usedSymbol = true };
        case R_X86_64_GLOB_DAT: return { .value = s, .length = sizeof(void*), .usedSymbol = true };
#elif __riscv_xlen == 64
        case R_RISCV_64: return { .value = a + s, .length = 8, .usedSymbol = true };
        case R_RISCV_32: return { .value = a + s, .length = 4, .usedSymbol = true };
        case R_RISCV_RELATIVE: return { .value = b + a, .length = sizeof(void*), .usedSymbol = false };
        case R_RISCV_JUMP_SLOT: return { .value = s, .length = sizeof(void*), .usedSymbol = true };
#elif __m68k__
        case R_68K_32: return { .value = s + a, .length = 4, .usedSymbol = true };
        case R_68K_16: return { .value = s + a, .length = 2, .usedSymbol = true };
        case R_68K_8: return { .value = s + a, .length = 1, .usedSymbol = true };
        case R_68K_PC32: return { .value = s + a - p, .length = 4, .usedSymbol = true };
        case R_68K_PC16: return { .value = s + a - p, .length = 2, .usedSymbol = true };
        case R_68K_PC8: return { .value = s + a - p, .length = 1, .usedSymbol = true };
        case R_68K_GLOB_DAT: return { .value = s, .length = 4, .usedSymbol = true };
        case R_68K_JMP_SLOT: return { .value = s, .length = 4, .usedSymbol = true };
        case R_68K_RELATIVE: return { .value = b + a, .length = 4, .usedSymbol = false };
#else
    #error "syslib/Elf.cpp: unknown architecture"
#endif
        }
        return { .value = 0, .length = 0, .usedSymbol = false };
    }

    Vector<const Elf_Phdr*> FindPhdrs(const Elf_Ehdr* hdr, Elf_Word type)
    {
        if (hdr == nullptr)
            return {};

        auto file = reinterpret_cast<const uint8_t*>(hdr);
        Vector<const Elf_Phdr*> found {};

        auto phdr = reinterpret_cast<const Elf_Phdr*>(file + hdr->e_phoff);
        for (size_t i = 0; i < hdr->e_phnum; i++)
        {
            if (phdr->p_type == type)
                found.PushBack(phdr);
            phdr = reinterpret_cast<const Elf_Phdr*>((uintptr_t)phdr + hdr->e_phentsize);
        }

        return found;
    }

    const Elf_Shdr* FindShdr(const Elf_Ehdr* hdr, const char* name)
    {
        if (hdr == nullptr || name == nullptr)
            return nullptr;

        auto file = reinterpret_cast<const uint8_t*>(hdr);
        const size_t nameLen = sl::memfirst(name, 0, 0);

        uintptr_t strtabOffset = hdr->e_shoff + (hdr->e_shstrndx * hdr->e_shentsize);
        auto stringTable = reinterpret_cast<const Elf_Shdr*>(file + strtabOffset);
        const char* strings = reinterpret_cast<const char*>(file + stringTable->sh_offset);
        
        auto scan = reinterpret_cast<const Elf_Shdr*>(file + hdr->e_shoff);
        for (size_t i = 0; i < hdr->e_shnum; i++)
        {
            const char* scanName = strings + scan->sh_name;
            const size_t scanLen = sl::memfirst(scanName, 0, 0);
            if (scanLen != nameLen || sl::memcmp(scanName, name, scanLen) != 0)
            {
                scan = reinterpret_cast<const Elf_Shdr*>((uintptr_t)scan + hdr->e_shentsize);
                continue;
            }

            return scan;
        }

        return nullptr;
    }

    sl::Vector<const Elf_Shdr*> FindShdrs(const Elf_Ehdr* hdr, Elf_Word type)
    {
        if (hdr == nullptr)
            return {};

        auto file = reinterpret_cast<const uint8_t*>(hdr);
        Vector<const Elf_Shdr*> found {};

        auto shdr = reinterpret_cast<const Elf_Shdr*>(file + hdr->e_shoff);
        for (size_t i = 0; i < hdr->e_shnum; i++)
        {
            if (shdr->sh_type == type)
                found.PushBack(shdr);
            shdr = reinterpret_cast<const Elf_Shdr*>((uintptr_t)shdr + hdr->e_shentsize);
        }

        return found;
    }
}

