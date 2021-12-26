#pragma once

#include <elf/Elf64.h>
#include <NativePtr.h>
#include <String.h>

namespace sl
{
    /*
        HeaderParser is useful for extracing info about an elf from an image. It does not modify or perform any loading/linking.
        For these things see elsewhere.
        This is more of a wrapper around the standard elf64 header, with some nice utilities built in.
    */
    class Elf64HeaderParser
    {
    private:
        const Elf64_Ehdr* header;
        Elf64_Shdr* stringTable;
        Elf64_Shdr* shdrStringTable;
        Elf64_Shdr* symbolTable;

    public:
        Elf64HeaderParser(NativePtr headerAddr);

        bool IsValidElf();
        Elf64_Sym* GetSymbol(NativePtr where);
        string GetSymbolName(NativePtr where);
        Elf64_Shdr* FindSectionHeader(string name);
    };
}
