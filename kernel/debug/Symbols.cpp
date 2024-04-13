#include <debug/Symbols.h>
#include <debug/Log.h>
#include <boot/LimineTags.h>
#include <formats/Elf.h>
#include <containers/LinkedList.h>
#include <Locks.h>

namespace Npk::Debug
{
    static SymbolFlag ClassifySymbol(const sl::Elf64_Sym& sym)
    {
        if (ELF64_ST_TYPE(sym.st_info) != sl::STT_FUNC)
            return SymbolFlag::NonFunction;

        const auto visibility = ELF64_ST_VISIBILITY(sym.st_other);
        if (visibility == sl::STV_HIDDEN || visibility == sl::STV_INTERNAL)
            return SymbolFlag::Private;
        if (visibility == sl::STV_DEFAULT && ELF64_ST_BIND(sym.st_info) == sl::STB_LOCAL)
            return SymbolFlag::Private;
        return SymbolFlag::Public;
    }

    static void ProcessElfSymbolTables(sl::NativePtr file, SymbolRepo& repo, uintptr_t loadBase)
    {
        auto ehdr = file.As<const sl::Elf64_Ehdr>();
        auto shdrs = file.As<const sl::Elf64_Shdr>(ehdr->e_shoff);
        auto symTables = sl::FindShdrs(ehdr, sl::SHT_SYMTAB);

        //first pass over the symbol tables: get counts for each category, make a copy of the string table.
        size_t countPublic = 0;
        size_t countPrivate = 0;
        size_t countOther = 0;
        for (size_t i = 0; i < symTables.Size(); i++)
        {
            const size_t symbolCount = symTables[i]->sh_size / symTables[i]->sh_entsize;
            auto syms = file.As<const sl::Elf64_Sym>(symTables[i]->sh_offset);

            //TODO: would be nice to just make a private mapping of this part of the file (0 copies),
            //instead of copying into anonymous memory.
            auto strTable = shdrs[symTables[i]->sh_link];
            ASSERT(!repo.stringTable.Valid(), "All symtabs must share the same strtab (for now)");
            repo.stringTable = VmObject(strTable.sh_size, VmFlag::Anon | VmFlag::Write);
            VALIDATE_(repo.stringTable.Valid(),);

            sl::memcopy(file.As<const void>(strTable.sh_offset), repo.stringTable->ptr, strTable.sh_size);
            repo.stringTable.Flags({}); //clear permissions flags, making string table readonly.

            for (size_t j = 0; j < symbolCount; j++)
            {
                if (syms[j].st_name == 0 || syms[j].st_size == 0)
                    continue; //ignore nameless and sizeless symbols

                switch (ClassifySymbol(syms[j]))
                {
                    case SymbolFlag::Public: countPublic++; break;
                    case SymbolFlag::Private: countPrivate++; break;
                    case SymbolFlag::NonFunction: countOther++; break;
                    default: ASSERT_UNREACHABLE();
                }
            }
        }

        repo.publicFunctions.EnsureCapacity(countPublic);
        repo.privateFunctions.EnsureCapacity(countPrivate);
        repo.nonFunctions.EnsureCapacity(countOther);

        //second iteration over symbol tables: store data about the symbols.
        for (size_t i = 0; i < symTables.Size(); i++)
        {
            const size_t symbolCount = symTables[i]->sh_size / symTables[i]->sh_entsize;
            auto syms = file.As<const sl::Elf64_Sym>(symTables[i]->sh_offset);

            for (size_t j = 0; j < symbolCount; j++)
            {
                if (syms[j].st_name == 0 || syms[j].st_size == 0)
                    continue; //ignore nameless and sizeless symbols

                KernelSymbol* storage = nullptr;
                switch (ClassifySymbol(syms[j]))
                {
                    case SymbolFlag::Public: 
                        storage = &repo.publicFunctions.EmplaceBack();
                        break;
                    case SymbolFlag::Private:
                        storage = &repo.privateFunctions.EmplaceBack();
                        break;
                    case SymbolFlag::NonFunction:
                        storage = &repo.nonFunctions.EmplaceBack();
                        break;
                    default: 
                        ASSERT_UNREACHABLE();
                }

                storage->base = loadBase + syms[j].st_value;
                storage->length = syms[j].st_size;
                const char* symbolName = repo.stringTable->As<const char>() + syms[j].st_name;
                storage->name = { symbolName, sl::memfirst(symbolName, 0, 0) };
            }
        }

        Log("Loaded symbols for %s: public=%lu, private=%lu, other=%lu", LogLevel::Info,
            repo.name.C_Str(), countPublic, countPrivate, countOther);
    }

    sl::RwLock repoListLock;
    sl::LinkedList<SymbolRepo> symbolRepos;
    SymbolStats totalStats;

    SymbolStats GetSymbolStats()
    {
        return totalStats;
    }

    void LoadKernelSymbols()
    {
        repoListLock.WriterLock();
        SymbolRepo& repo = symbolRepos.EmplaceBack();
        repoListLock.WriterUnlock();
        repo.name = "kernel";
        
        if (Boot::kernelFileRequest.response == nullptr)
        {
            //TODO: other ways to load kernel symbols
            Log("Bootloader did not provide kernel file feature response", LogLevel::Error);
            return;
        }

        void* kernelFileAddr = Boot::kernelFileRequest.response->kernel_file->address;
        ProcessElfSymbolTables(kernelFileAddr, repo, 0);

        totalStats.publicCount = repo.publicFunctions.Size();
        totalStats.privateCount = repo.privateFunctions.Size();
        totalStats.otherCount = repo.nonFunctions.Size();
    }

    sl::Handle<SymbolRepo> LoadElfModuleSymbols(sl::StringSpan name, VmObject& file, uintptr_t loadBase)
    {
        repoListLock.WriterLock();
        SymbolRepo& repo = symbolRepos.EmplaceBack();
        repo.name = name;
        ProcessElfSymbolTables(file->ptr, repo, loadBase);

        totalStats.publicCount += repo.publicFunctions.Size();
        totalStats.privateCount += repo.privateFunctions.Size();
        totalStats.otherCount += repo.nonFunctions.Size();
        repoListLock.WriterUnlock();

        return sl::Handle<SymbolRepo>(&repo); //TODO: delete[] wont properly remove repo from list (or fix symbol counts)
    }

    static sl::Opt<KernelSymbol> LocateSymbol(sl::Opt<uintptr_t> addr, sl::StringSpan name, SymbolFlags flags, sl::StringSpan* repoName)
    {
        auto CheckSymbolList = [=](sl::Vector<KernelSymbol>& list)
        {
            for (size_t i = 0; i < list.Size(); i++)
            {
                if (name == list[i].name)
                    return sl::Opt<KernelSymbol>(list[i]);
                if (addr.HasValue() && *addr >= list[i].base && *addr < list[i].base + list[i].length)
                    return sl::Opt<KernelSymbol>(list[i]);
            }

            return sl::Opt<KernelSymbol>{};
        };

        if (!flags.Any())
            return {};

        sl::Opt<KernelSymbol> found {};
        repoListLock.ReaderLock();
        for (auto it = symbolRepos.Begin(); it != symbolRepos.End(); ++it)
        {
            if (flags.Has(SymbolFlag::Public))
            {
                found = CheckSymbolList(it->publicFunctions);
                if (found.HasValue())
                {
                    if (repoName != nullptr)
                        *repoName = it->name.Span();
                    break;
                }
            }
            if (flags.Has(SymbolFlag::Private))
            {
                found = CheckSymbolList(it->privateFunctions);
                if (found.HasValue())
                {
                    if (repoName != nullptr)
                        *repoName = it->name.Span();
                    break;
                }
            }
            if (flags.Has(SymbolFlag::NonFunction))
            {
                found = CheckSymbolList(it->nonFunctions);
                if (found.HasValue())
                {
                    if (repoName != nullptr)
                        *repoName = it->name.Span();
                    break;
                }
            }

            //only search for kernel symbols: this works because the first repo is always the kernel.
            if (flags.Has(SymbolFlag::Kernel))
                break;
        }
        repoListLock.ReaderUnlock();

        return found;
    }

    sl::Opt<KernelSymbol> SymbolFromAddr(uintptr_t addr, SymbolFlags flags, sl::StringSpan* repoName)
    {
        return LocateSymbol(addr, {}, flags, repoName);
    }

    sl::Opt<KernelSymbol> SymbolFromName(sl::StringSpan name, SymbolFlags flags, sl::StringSpan* repoName)
    {
        return LocateSymbol({}, name, flags, repoName);
    }
}
