#include <debug/Symbols.h>
#include <debug/Log.h>
#include <interfaces/loader/Generic.h>
#include <formats/Elf.h>
#include <containers/LinkedList.h>
#include <Locks.h>

namespace Npk::Debug
{
    static SymbolFlag ClassifySymbol(const sl::Elf_Sym& sym)
    {
        if (ELF_ST_TYPE(sym.st_info) != sl::STT_FUNC)
            return SymbolFlag::NonFunction;

        const auto visibility = ELF_ST_VISIBILITY(sym.st_other);
        if (visibility == sl::STV_HIDDEN || visibility == sl::STV_INTERNAL)
            return SymbolFlag::Private;
        if (visibility == sl::STV_DEFAULT && ELF_ST_BIND(sym.st_info) == sl::STB_LOCAL)
            return SymbolFlag::Private;
        return SymbolFlag::Public;
    }

    static void ProcessElfSymbolTable(sl::Span<const sl::Elf_Sym> symtab, sl::Span<const char> strtab, SymbolRepo& repo, uintptr_t loadBase)
    {
        //make a copy of the string table
        repo.stringTable = VmObject(strtab.Size(), VmFlag::Anon | VmFlag::Write);
        VALIDATE_(repo.stringTable.Valid(), );
        sl::memcopy(strtab.Begin(), repo.stringTable->ptr, strtab.Size());
        repo.stringTable.Flags({}); //clear permissions, making string table readonly
        
        size_t countPublic = 0;
        size_t countPrivate = 0;
        size_t countOther = 0;
        for (size_t i = 0; i < symtab.Size(); i++)
        {
            if (symtab[i].st_name == 0 || symtab[i].st_size == 0)
                continue; //ignore nameless or sizeless symbols

            switch (ClassifySymbol(symtab[i]))
            {
                case SymbolFlag::Public: countPublic++; break;
                case SymbolFlag::Private: countPrivate++; break;
                case SymbolFlag::NonFunction: countOther++; break;
                default: ASSERT_UNREACHABLE();
            }
        }

        repo.publicFunctions.EnsureCapacity(countPublic);
        repo.privateFunctions.EnsureCapacity(countPrivate);
        repo.nonFunctions.EnsureCapacity(countOther);

        for (size_t i = 0; i < symtab.Size(); i++)
        {
            if (symtab[i].st_name == 0 || symtab[i].st_size == 0)
                continue;

            KernelSymbol* storage;
            switch (ClassifySymbol(symtab[i]))
            {
                case SymbolFlag::Public: storage = &repo.publicFunctions.EmplaceBack(); break;
                case SymbolFlag::Private: storage = &repo.privateFunctions.EmplaceBack(); break;
                case SymbolFlag::NonFunction: storage = &repo.nonFunctions.EmplaceBack(); break;
                default: ASSERT_UNREACHABLE();
            }

            storage->base = loadBase + symtab[i].st_value;
            storage->length = symtab[i].st_size;
            const char* symName = repo.stringTable->As<const char>() + symtab[i].st_name;
            storage->name = { symName, sl::memfirst(symName, 0, 0) };
        }

        Log("Loaded symbols for %s: public=%zu, private=%zu, other=%zu", LogLevel::Info,
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

        const sl::Span<uint8_t> symtab = GetKernelSymbolTable();
        const sl::Span<const char> strtab = GetKernelStringTable();

        VALIDATE_(!symtab.Empty(), );
        VALIDATE_(!strtab.Empty(), );

        const sl::Span<const sl::Elf_Sym> realSymtab 
        { 
            reinterpret_cast<const sl::Elf_Sym*>(symtab.Begin()),
            symtab.SizeBytes() / sizeof(sl::Elf_Sym) 
        };
        ProcessElfSymbolTable(realSymtab, strtab, repo, 0);

        totalStats.publicCount = repo.publicFunctions.Size();
        totalStats.privateCount = repo.privateFunctions.Size();
        totalStats.otherCount = repo.nonFunctions.Size();
    }

    sl::Handle<SymbolRepo> LoadElfModuleSymbols(sl::StringSpan name, VmObject& file, uintptr_t loadBase)
    {
        repoListLock.WriterLock();
        SymbolRepo& repo = symbolRepos.EmplaceBack();
        repoListLock.WriterUnlock();
        repo.name = name;

        auto ehdr = file->As<const sl::Elf_Ehdr>();
        auto shdrs = file->Offset(ehdr->e_shoff).As<sl::Elf_Shdr>();
        for (size_t i = 0; i < ehdr->e_shnum; i++)
        {
            if (shdrs[i].sh_type != sl::SHT_SYMTAB)
                continue;

            sl::Span<const sl::Elf_Sym> symtab 
            {
                file->Offset(shdrs[i].sh_offset).As<sl::Elf_Sym>(),
                shdrs[i].sh_size / sizeof(sl::Elf_Sym)
            };

            sl::Span<const char> strtab
            {
                file->Offset(shdrs[shdrs[i].sh_link].sh_offset).As<const char>(),
                shdrs[shdrs[i].sh_link].sh_size
            };

            ProcessElfSymbolTable(symtab, strtab, repo, loadBase);
            break;
        }

        totalStats.publicCount += repo.publicFunctions.Size();
        totalStats.privateCount += repo.privateFunctions.Size();
        totalStats.otherCount += repo.nonFunctions.Size();

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
