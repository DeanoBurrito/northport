#include <debug/Symbols.h>
#include <debug/Log.h>
#include <boot/LimineTags.h>
#include <formats/Elf.h>
#include <NativePtr.h>
#include <Span.h>
#include <Locks.h>

namespace Npk::Debug
{
    sl::RwLock symbolsLock;
    sl::Span<KernelSymbol> symbols;

    size_t ScanSymbolTables(sl::NativePtr file, bool scanOnly)
    {
        auto ehdr = file.As<const sl::Elf64_Ehdr>();
        auto shdrs = file.As<const sl::Elf64_Shdr>(ehdr->e_shoff);
        sl::Vector<const sl::Elf64_Shdr*> symTables = sl::FindShdrs(ehdr, sl::SHT_SYMTAB);

        size_t namedSymbolsCount = 0;
        for (size_t i = 0; i < symTables.Size(); i++)
        {
            const size_t symbolCount = symTables[i]->sh_size / symTables[i]->sh_entsize;
            auto syms = file.As<const sl::Elf64_Sym>(symTables[i]->sh_offset);
            const char* strings = file.As<const char>(shdrs[symTables[i]->sh_link].sh_offset);

            for (size_t j = 0; j < symbolCount; j++)
            {
                if (syms[j].st_name == 0 || syms[j].st_size == 0)
                    continue; //ignore unnamed or sizeless symbols.

                const size_t accessor = namedSymbolsCount++;
                if (scanOnly)
                    continue;

                symbols[accessor].length = syms[j].st_size;
                const char* symName = strings + syms[j].st_name;
                symbols[accessor].name = { symName, sl::memfirst(symName, 0, 0) };

                if (syms[j].st_shndx == sl::SHN_ABS)
                    symbols[accessor].base = syms[j].st_value;
                else if (syms[j].st_shndx < ehdr->e_shnum)
                    symbols[accessor].base = syms[j].st_value; //TODO: handle symbols when the kernel has been relocated
                else
                    Log("Kernel symbol %s has unknown address", LogLevel::Verbose, strings + syms[j].st_name);
            }
        }

        return namedSymbolsCount;
    }

    void LoadKernelSymbols()
    {
        if (Boot::kernelFileRequest.response == nullptr)
        {
            Log("Failed to load kernel symbols, file request not present", LogLevel::Warning);
            return;
        }

        void* fileAddr = Boot::kernelFileRequest.response->kernel_file->address;
        if (!sl::ValidateElfHeader(fileAddr, sl::ET_EXEC))
        {
            Log("Failed to load kernel symbols, bad image header.", LogLevel::Warning);
            return;
        }

        symbolsLock.WriterLock();
        const size_t symbolCount = ScanSymbolTables(fileAddr, true);
        symbols = sl::Span<KernelSymbol>(new KernelSymbol[symbolCount], symbolCount);
        ScanSymbolTables(fileAddr, false);
        symbolsLock.WriterUnlock();

        Log("Loaded %lu kernel symbols.", LogLevel::Info, symbols.Size());
    }

    sl::Opt<KernelSymbol> SymbolFromAddr(uintptr_t addr)
    {
        if (symbols.Empty())
            return {};

        symbolsLock.ReaderLock();
        for (size_t i = 0; i < symbols.Size(); i++)
        {
            if (addr >= symbols[i].base && addr < symbols[i].base + symbols[i].length)
            {
                KernelSymbol sym = symbols[i];
                symbolsLock.ReaderUnlock();
                return sym;
            }
        }
        symbolsLock.ReaderUnlock();

        return {}; //TODO: allow for parsing module symbol tables
    }

    sl::Opt<KernelSymbol> SymbolFromName(sl::StringSpan name)
    {
        if (symbols.Empty() || name.Empty())
            return {};

        symbolsLock.ReaderLock();
        for (size_t i = 0; i < symbols.Size(); i++)
        {
            if (symbols[i].name == name)
            {
                KernelSymbol sym = symbols[i];
                symbolsLock.ReaderUnlock();
                return sym;
            }
        }
        symbolsLock.ReaderUnlock();

        return {};
    }
}
