#include <elf/HeaderParser.h>
#include <elf/Demangle.h>
#include <Memory.h>
#include <Maths.h>
#include <Memory.h>

namespace sl
{
    Elf64HeaderParser::Elf64HeaderParser(NativePtr headerAddr)
    {
        if (headerAddr.ptr == nullptr)
        {
            header = nullptr;
            return;
        }

        header = headerAddr.As<Elf64_Ehdr>();

        shdrStringTable = nullptr;
        Elf64_Shdr* sectionHeaders = sl::NativePtr((void*)header).As<Elf64_Shdr>(header->e_shoff);
        shdrStringTable = &sectionHeaders[header->e_shstrndx];

        symbolTable = *FindSectionHeader(".symtab");
        stringTable = *FindSectionHeader(".strtab");
    }

    bool Elf64HeaderParser::IsValidElf() const
    {
        return memcmp(header->e_ident, ExpectedMagic, 4) == 0;
    }

    sl::Opt<Elf64_Sym*> Elf64HeaderParser::GetSymbol(NativePtr where) const
    {
        if (header == nullptr)
            return {};
        
        Elf64_Sym* symbols = sl::NativePtr((void*)header).As<Elf64_Sym>(symbolTable->sh_offset);
        const size_t symbolCount = symbolTable->sh_size / symbolTable->sh_entsize;

        for (size_t i = 1; i < symbolCount; i++)
        {
            if (symbols[i].st_value == where.raw)
                return &symbols[i]; //exact match (like an absolute symbol)
            
            const size_t symbolTop = symbols[i].st_value + symbols[i].st_size;
            if (symbols[i].st_value <= where.raw && symbolTop >= where.raw)
                return &symbols[i]; //symbol contains this pointer within it
        }
        
        return {};
    }

    string Elf64HeaderParser::GetSymbolName(NativePtr where) const
    {
        sl::Opt<Elf64_Sym*> sym = GetSymbol(where);

        if (!sym)
            return "<unknown symbol>";

        //DemangleName() will return the original name if it couldn't demangle successfully
        string rawName = sl::NativePtr((size_t)header + stringTable->sh_offset).As<const char>(sym.Value()->st_name);
        return DemangleName(rawName);
    }

    sl::Opt<Elf64_Shdr*> Elf64HeaderParser::FindSectionHeader(const sl::String& name) const
    {
        if (header == nullptr)
            return {};
        
        Elf64_Shdr* shdrs = sl::NativePtr((void*)header).As<Elf64_Shdr>(header->e_shoff);

        for (size_t i = 0; i < header->e_shnum; i++)
        {
            if (shdrs[i].sh_name == 0)
                continue; //will be an empty string, skip it

            const char* sectionName = sl::NativePtr((size_t)header + shdrStringTable->sh_offset).As<const char>(shdrs[i].sh_name);
            const size_t nameLength = sl::memfirst(sectionName, 0, 0);
            if (nameLength != name.Size())
                continue;

            if (sl::memcmp(sectionName, name.C_Str(), sl::min(nameLength, name.Size())) == 0)
                return &shdrs[i];
        }
        
        return {};
    }

    sl::Vector<Elf64_Phdr*> Elf64HeaderParser::FindProgramHeaders(sl::Opt<Elf64_Word> type) const
    {
        if (header == nullptr)
            return {};

        sl::Vector<Elf64_Phdr*> foundHeaders;
        Elf64_Phdr* phdrs = sl::NativePtr((void*)header).As<Elf64_Phdr>(header->e_phoff);

        for (size_t i = 0; i < header->e_phnum; i++)
        {
            if (type.HasValue() && *type != phdrs[i].p_type)
                continue;

            foundHeaders.PushBack(&phdrs[i]);
        }

        return foundHeaders;
    }
}
