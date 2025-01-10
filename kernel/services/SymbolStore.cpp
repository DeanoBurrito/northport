#include <services/SymbolStore.h>
#include <interfaces/loader/Generic.h>
#include <core/WiredHeap.h>
#include <core/Log.h>
#include <services/Vmm.h>
#include <formats/Elf.h>
#include <Memory.h>

namespace Npk::Services
{
    sl::RwLock reposLock;
    sl::List<SymbolRepo, &SymbolRepo::hook> symbolRepos;
    
    static bool IsPublicSymbol(sl::Elf_Sym sym)
    {
        const auto visibility = ELF_ST_VISIBILITY(sym.st_other);
        const auto binding = ELF_ST_BIND(sym.st_info);
        
        return !(visibility == sl::STV_HIDDEN || visibility == sl::STV_INTERNAL
            || (visibility == sl::STV_DEFAULT && binding == sl::STB_LOCAL));
    }

    void LoadKernelSymbols()
    {
        SymbolRepo* kernelRepo = NewWired<SymbolRepo>();
        VALIDATE_(kernelRepo != nullptr, );

        //TODO: we assume the kernel is an elf, what if its not?
        auto symTable = GetKernelSymbolTable();
        auto strTable = GetKernelStringTable();
        const size_t symCount = symTable.Size() / sizeof(sl::Elf_Sym);
        auto syms = reinterpret_cast<const sl::Elf_Sym*>(symTable.Begin());

        size_t publicSymbols = 0;
        size_t privateSymbols = 0;
        size_t stringsLength = 1;
        for (size_t i = 0; i < symCount; i++)
        {
            const auto sym = syms[i];
            if (ELF_ST_TYPE(sym.st_info) != sl::STT_FUNC)
                continue; //we only care about functions

            (IsPublicSymbol(sym) ? publicSymbols : privateSymbols)++;
            stringsLength += sl::memfirst(strTable.Begin() + sym.st_name, 0, 0);
        }

        const size_t allocSize = (privateSymbols + publicSymbols) *
            sizeof(SymbolInfo) + stringsLength;
        auto maybeStore = VmAllocAnon(allocSize, VmViewFlag::Write);
        VALIDATE_(maybeStore.HasValue(), );

        SymbolInfo* publicStore = static_cast<SymbolInfo*>(*maybeStore);
        SymbolInfo* privateStore = publicStore + publicSymbols;
        char* stringsStore = reinterpret_cast<char*>(privateStore + privateSymbols);

        publicSymbols = privateSymbols = 0;
        stringsLength = 1;
        stringsStore[0] = 0;
        kernelRepo->base = (uintptr_t)-1;
        kernelRepo->length = 0;

        for (size_t i = 0; i < symCount; i++)
        {
            const auto sym = syms[i];
            if (ELF_ST_TYPE(sym.st_info) != sl::STT_FUNC)
                continue;

            SymbolInfo* symStore = 
                (IsPublicSymbol(sym) ? &publicStore[publicSymbols++] : &privateStore[privateSymbols++]);
            symStore->base = sym.st_value;
            symStore->length = sym.st_size;

            kernelRepo->base = sl::Min(sym.st_value, kernelRepo->base);
            kernelRepo->length = sl::Max(sym.st_value + sym.st_size, kernelRepo->length);

            const size_t nameLength = sl::memfirst(&strTable[sym.st_name], 0, strTable.Size() - sym.st_name);
            sl::memcopy(&strTable[sym.st_name], &stringsStore[stringsLength], nameLength);
            symStore->name = sl::StringSpan(&stringsStore[stringsLength], nameLength);
            stringsLength += nameLength;
        }

        kernelRepo->length -= kernelRepo->base;
        kernelRepo->pubFuncs = sl::Span<const SymbolInfo>(publicStore, publicSymbols);
        kernelRepo->privFuncs = sl::Span<const SymbolInfo>(privateStore, privateSymbols);
        kernelRepo->stringTable = sl::StringSpan(stringsStore, stringsLength);
        kernelRepo->name = "kernel";

        Log("Kernel symbols loaded: public=%zu, private=%zu, strings=%zu B", LogLevel::Info,
            publicSymbols, privateSymbols, stringsLength);

        reposLock.WriterLock();
        symbolRepos.InsertSorted(kernelRepo, [](auto* a, auto* b) { return a->base < b->base; });
        reposLock.WriterUnlock();
    }

    sl::Opt<SymbolView> FindSymbol(uintptr_t addr)
    {
        sl::Opt<SymbolView> ret {};

        reposLock.ReaderLock();
        for (auto it = symbolRepos.Begin(); it != symbolRepos.End(); ++it)
        {
            if (it->base > addr)
                break;
            if (addr > it->base + it->length)
                continue;

            for (size_t i = 0; i < it->pubFuncs.Size(); i++)
            {
                if (it->pubFuncs[i].base > addr)
                    //break;
                    continue;
                if (addr > it->pubFuncs[i].base + it->pubFuncs[i].length)
                    continue;

                ret = SymbolView { .repo = &*it, .info = &it->pubFuncs[i] };
                break;
            }
            if (ret.HasValue())
                break;

            for (size_t i = 0; i < it->privFuncs.Size(); i++)
            {
                if (it->privFuncs[i].base > addr)
                    //break; //TODO: this breaks because symbols arent always ordered by the linker
                    continue;
                if (addr > it->privFuncs[i].base + it->privFuncs[i].length)
                    continue;

                ret = SymbolView { .repo = &*it, .info = &it->privFuncs[i] };
                break;
            }
            if (ret.HasValue())
                break;
        }
        reposLock.ReaderUnlock();

        return ret;
    }

    sl::Opt<SymbolView> FindSymbol(sl::StringSpan name)
    {
        sl::Opt<SymbolView> ret {};

        reposLock.ReaderLock();
        for (auto it = symbolRepos.Begin(); it != symbolRepos.End(); ++it)
        {
            for (size_t i = 0; i < it->pubFuncs.Size(); i++)
            {
                if (it->pubFuncs[i].name != name)
                    continue;

                ret = SymbolView { .repo = &*it, .info = &it->pubFuncs[i] };
                break;
            }
            if (ret.HasValue())
                break;

            for (size_t i = 0; i < it->privFuncs.Size(); i++)
            {
                if (it->privFuncs[i].name != name)
                    continue;

                ret = SymbolView { .repo = &*it, .info = &it->privFuncs[i] };
                break;
            }
            if (ret.HasValue())
                break;
        }
        reposLock.ReaderUnlock();

        return ret;
    }

    sl::Opt<uintptr_t> FindDriverApiSymbol(sl::StringSpan name, bool kernelOnly)
    {
        sl::Opt<uintptr_t> ret {};

        reposLock.ReaderLock();
        for (auto it = symbolRepos.Begin(); it != symbolRepos.End(); ++it)
        {
            for (size_t i = 0; i < it->pubFuncs.Size(); i++)
            {
                if (it->pubFuncs[i].name != name)
                    continue;

                ret = it->pubFuncs[i].base;
                break;
            }
            if (ret.HasValue() || kernelOnly) //kernel repo is always the first in the list
                break;
        }
        reposLock.ReaderUnlock();

        return ret;
    }
}
