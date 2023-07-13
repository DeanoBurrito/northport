#include <formats/Elf.h>

namespace sl
{
    bool ValidateElfHeader(const void* file, Elf64_Half type)
    {
#ifdef __x86_64__
        constexpr Elf64_UnsignedChar elfClass = ELFCLASS64;
        constexpr Elf64_UnsignedChar elfData = ELFDATA2LSB;
        constexpr Elf64_Half elfMach = EM_X86_64;
#elif __riscv_xlen == 64
        constexpr Elf64_UnsignedChar elfClass = ELFCLASS64;
        constexpr Elf64_UnsignedChar elfData = ELFDATA2LSB;
        constexpr Elf64_Half elfMach = EM_RISCV;
#else
    #warning "syslib/Elf.cpp: Unknown architecture"

    constexpr Elf64_UnsignedChar elfClass = 0;
    constexpr Elf64_UnsignedChar elfData = 0;
    constexpr Elf64_Half elfMach = 0;
    return false;
#endif

        auto hdr = reinterpret_cast<const Elf64_Ehdr*>(file);
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

    Vector<const Elf64_Phdr*> FindPhdrs(const Elf64_Ehdr* hdr, Elf64_Word type)
    {
        if (hdr == nullptr)
            return {};

        auto file = reinterpret_cast<const uint8_t*>(hdr);
        Vector<const Elf64_Phdr*> found {};

        auto phdr = reinterpret_cast<const Elf64_Phdr*>(file + hdr->e_phoff);
        for (size_t i = 0; i < hdr->e_phnum; i++)
        {
            if (phdr->p_type == type)
                found.PushBack(phdr);
            phdr = reinterpret_cast<const Elf64_Phdr*>((uintptr_t)phdr + hdr->e_phentsize);
        }

        return found;
    }

    const Elf64_Shdr* FindShdr(const Elf64_Ehdr* hdr, const char* name)
    {
        if (hdr == nullptr || name == nullptr)
            return nullptr;

        auto file = reinterpret_cast<const uint8_t*>(hdr);
        const size_t nameLen = sl::memfirst(name, 0, 0);

        uintptr_t strtabOffset = hdr->e_shoff + (hdr->e_shstrndx * hdr->e_shentsize);
        auto stringTable = reinterpret_cast<const Elf64_Shdr*>(file + strtabOffset);
        const char* strings = reinterpret_cast<const char*>(file + stringTable->sh_offset);
        
        auto scan = reinterpret_cast<const Elf64_Shdr*>(file + hdr->e_shoff);
        for (size_t i = 0; i < hdr->e_shnum; i++)
        {
            const char* scanName = strings + scan->sh_name;
            const size_t scanLen = sl::memfirst(scanName, 0, 0);
            if (scanLen != nameLen || sl::memcmp(scanName, name, scanLen) != 0)
            {
                scan = reinterpret_cast<const Elf64_Shdr*>((uintptr_t)scan + hdr->e_shentsize);
                continue;
            }

            return scan;
        }

        return nullptr;
    }
}

