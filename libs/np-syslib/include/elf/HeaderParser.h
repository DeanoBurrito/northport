#pragma once

#include <elf/Elf64.h>
#include <NativePtr.h>
#include <Optional.h>
#include <containers/Vector.h>
#include <String.h>

//pointer to the elf of the currently running program
extern sl::NativePtr currentProgramElf;

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

        bool IsValidElf() const;
        string GetSymbolName(NativePtr where) const;
        sl::Opt<Elf64_Sym*> GetSymbol(NativePtr where) const;
        sl::Opt<Elf64_Shdr*> FindSectionHeader(const sl::String& name) const;
        sl::Vector<Elf64_Phdr*> FindProgramHeaders(sl::Opt<Elf64_Word> type) const;
    };
}
