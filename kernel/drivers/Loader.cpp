#include <drivers/Loader.h>
#include <drivers/api/Api.h>
#include <debug/Log.h>
#include <filesystem/Filesystem.h>
#include <memory/Vmm.h>
#include <formats/Elf.h>
#include <Memory.h>

namespace Npk::Drivers
{
    constexpr const char* LoadTypeStrs[] = { "never", "always", "pci-class", "pci-device", "dtb-compat" };

    const void* FindByGuid(sl::Span<const uint8_t> section, sl::Span<const uint8_t> guid)
    {
        if (section.Size() == 0)
            return nullptr;
        
        for (size_t i = 0; i < section.Size() - guid.Size(); i++)
        {
            if (section[i] != guid[0])
                continue;

            bool success = true;
            for (size_t g = 0; g < guid.Size(); g++)
            {
                if (section[i + g] != guid[g])
                {
                    i += g - 1;
                    success = false;
                    break;
                }
            }

            if (success)
                return static_cast<const void*>(section.Begin() + i);
        }

        return nullptr;
    }

    bool LoadModule(LoadableModule& module)
    {
        if (module.image.Valid())
            return true;

        //TODO: have vfs vm driver set the length for us, we can skip the stat() calls
        auto node = Filesystem::VfsLookup(module.filepath.Span(), Filesystem::KernelFsCtxt);
        VALIDATE_(node.Valid(), false);
        VALIDATE_(node->type == Filesystem::NodeType::File, false);

        Filesystem::NodeProps props;
        VALIDATE_(node->GetProps(props, Filesystem::KernelFsCtxt), false);

        Memory::VmoFileInitArg vmoArg {};
        vmoArg.offset = 0;
        vmoArg.filepath = module.filepath.Span();
        module.image = { props.size, reinterpret_cast<uintptr_t>(&vmoArg), VmFlag::File | VmFlag::Write };
        VALIDATE_(module.image.Valid(), false);
        VALIDATE_(sl::ValidateElfHeader(module.image->ptr, sl::ET_REL), false);

        sl::StringSpan moduleName = module.filepath.Span();
        while (true)
        {
            const size_t index = sl::memfirst(moduleName.Begin(), '/', moduleName.Size());
            if (index == -1ul)
                break;
            moduleName = moduleName.Subspan(index + 1, -1ul);
        }
        Log("Loading module image: %s (filepath: %s)", LogLevel::Info, moduleName.Begin(), module.filepath.C_Str());

        const sl::Elf64_Ehdr* ehdr = module.image->As<const sl::Elf64_Ehdr>();
        sl::Span<sl::Elf64_Shdr> shdrs(module.image->As<sl::Elf64_Shdr>(ehdr->e_shoff), ehdr->e_shnum);
        const char* shdrNames = module.image->As<const char>(shdrs[ehdr->e_shstrndx].sh_offset);

        //TODO: map drivers as PRIVATE so any alternations we make arent propagated to the original file.
        for (size_t i = 0; i < shdrs.Size(); i++)
        {
            if (shdrs[i].sh_type == sl::SHT_NOBITS)
            {
                auto maybeVaddr = VMM::Kernel().Alloc(shdrs[i].sh_size, 0, VmFlag::Anon | VmFlag::Write);
                VALIDATE_(maybeVaddr.HasValue(), false);
                shdrs[i].sh_addr = *maybeVaddr;
                Log("Module %s section allocated: 0x%lx 0x%04lx %s", LogLevel::Verbose, moduleName.Begin(),
                    *maybeVaddr, shdrs[i].sh_size, shdrNames + shdrs[i].sh_name);
            }
            else
            {
                shdrs[i].sh_addr = module.image->raw + shdrs[i].sh_offset;
                Log("Module %s section mapped: 0x%lx 0x%04lx %s", LogLevel::Verbose, 
                    moduleName.Begin(), shdrs[i].sh_addr, shdrs[i].sh_size, shdrNames + shdrs[i].sh_name);
            }
        }

        auto symtabs = sl::FindShdrs(ehdr, sl::SHT_SYMTAB);
        for (size_t i = 0; i < symtabs.Size(); i++)
        {
            const char* strings = module.image->As<const char>(shdrs[symtabs[i]->sh_link].sh_offset);
            auto syms = reinterpret_cast<sl::Elf64_Sym*>(symtabs[i]->sh_addr);

            const size_t symbolCount = symtabs[i]->sh_size / symtabs[i]->sh_entsize;
            for (size_t s = 0; s < symbolCount; s++)
            {
                if (syms[s].st_shndx == sl::SHN_UNDEF)
                {
                    if (syms[s].st_name != 0)
                        Log("TODO: external symbol needed: %s", LogLevel::Debug, strings + syms[s].st_name);
                }
                else if (syms[s].st_shndx < sl::SHN_LOPROC)
                    syms[s].st_value = syms[s].st_value + shdrs[syms[s].st_shndx].sh_addr;

            }
            Log("Loaded %lu symbols for module %s", LogLevel::Verbose, symbolCount, module.filepath.C_Str());
        }

        for (size_t i = 0; i < shdrs.Size(); i++)
        {
            if (shdrs[i].sh_type == sl::SHT_REL)
            {
                Log("REL type relocations not supported by module loader", LogLevel::Warning);
                continue;
            }
            else if (shdrs[i].sh_type != sl::SHT_RELA)
                continue;

            sl::NativePtr target(shdrs.Begin() + shdrs[i].sh_info);
            auto symbols = reinterpret_cast<sl::Elf64_Sym*>(shdrs[shdrs[i].sh_link].sh_addr);
            auto relas = reinterpret_cast<const sl::Elf64_Rela*>(shdrs[i].sh_addr);

            const size_t relaCount = shdrs[i].sh_size / shdrs[i].sh_entsize;
            for (size_t r = 0; r < relaCount; r++)
            {
                sl::NativePtr fixpoint = target.Offset(relas[r].r_offset);
                const uintptr_t s = symbols[ELF64_R_SYM(relas[r].r_info)].st_value;
                const uintptr_t p = fixpoint.raw;
                const uintptr_t a = relas[r].r_addend;

                auto reloc = sl::ComputeRelocation(ELF64_R_TYPE(relas[r].r_info), a, s, p);
                VALIDATE(reloc.mask != 0, false, "Unknown elf relocation type");
                const uintptr_t value = fixpoint.Read<uintptr_t>() & ~reloc.mask;
                fixpoint.Write<uintptr_t>(value | (reloc.value & reloc.mask));
            }

            Log("Applied %lu relocations to module %s", LogLevel::Verbose, relaCount,
                module.filepath.C_Str());
        }

        return true;
    }

    bool UnloadModule(LoadableModule& module)
    {
        ASSERT_UNREACHABLE();
    }

    sl::Opt<const sl::Elf64_Shdr*> LoadModuleMetadata(VmObject& file)
    {
        VALIDATE_(file.Valid(), {});
        VALIDATE_(sl::ValidateElfHeader(file->ptr, sl::ET_REL), {});

        auto ehdr = file->As<const sl::Elf64_Ehdr>();
        auto shdrs = file->As<sl::Elf64_Shdr>(ehdr->e_shoff);
        auto metadataShdr = sl::FindShdr(ehdr, ".npk_module");
        VALIDATE_(metadataShdr != nullptr, {});
        const size_t metadataIndex = ((uintptr_t)metadataShdr - (file->raw + ehdr->e_shoff)) / ehdr->e_shentsize;

        //Populate the in-memory addresses of all section headers. No need to allocate NOBITS sections,
        //as they wont contain anything useful to us here.
        for (size_t i = 0; i < ehdr->e_shnum; i++)
        {
            if (shdrs[i].sh_type == sl::SHT_NOBITS)
                continue;
            shdrs[i].sh_addr = file->raw + shdrs[i].sh_offset;
        }

        //resolve any internal symbols, we ignore external ones for now - since we arent actually
        //running the module in this form (we only want the metadata, which must consist of internal symbols).
        auto symtabs = sl::FindShdrs(ehdr, sl::SHT_SYMTAB);
        for (size_t i = 0; i < symtabs.Size(); i++)
        {
            const char* strings = file->As<const char>(shdrs[symtabs[i]->sh_link].sh_offset);
            auto syms = reinterpret_cast<sl::Elf64_Sym*>(symtabs[i]->sh_addr);

            const size_t symbolCount = symtabs[i]->sh_size / symtabs[i]->sh_entsize;
            for (size_t s = 0; s < symbolCount; s++)
            {
                if (syms[s].st_shndx == sl::SHN_UNDEF || syms[s].st_shndx >= sl::SHN_LOPROC)
                    continue;
                syms[s].st_value += shdrs[syms[s].st_shndx].sh_addr;
            }
        }

        VALIDATE_(sl::ApplySectionRelocations(ehdr, metadataIndex), {});
        return metadataShdr;
    }

    bool ScanForDrivers(sl::StringSpan filepath)
    {
        Log("Checking kernel module candidate: %s", LogLevel::Verbose, filepath.Begin());

        auto node = Filesystem::VfsLookup(filepath, Filesystem::KernelFsCtxt);
        VALIDATE_(node.Valid(), false);
        VALIDATE_(node->type == Filesystem::NodeType::File, false);

        Filesystem::NodeProps props;
        VALIDATE_(node->GetProps(props, Filesystem::KernelFsCtxt), false);

        Memory::VmoFileInitArg vmoArg {};
        vmoArg.filepath = filepath;
        VmObject file { props.size, reinterpret_cast<uintptr_t>(&vmoArg), VmFlag::File | VmFlag::Write };
        VALIDATE_(file.Valid(), false);
        VALIDATE_(sl::ValidateElfHeader(file->ptr, sl::ET_REL), false);

        auto maybeMetadata = LoadModuleMetadata(file);
        VALIDATE_(maybeMetadata.HasValue(), false);
        sl::Span<const uint8_t> metadata(reinterpret_cast<const uint8_t*>(maybeMetadata.Value()->sh_addr),
            maybeMetadata.Value()->sh_size);

        const uint8_t moduleHdrGuid[] = NP_MODULE_META_START_GUID;
        auto moduleMetadata = static_cast<const npk_module_metadata*>(FindByGuid(metadata, moduleHdrGuid));
        VALIDATE_(moduleMetadata != nullptr, false);

        //scan the metadata section for driver manifests
        const uint8_t manifestHdrGuid[] = NP_MODULE_MANIFEST_GUID;
        sl::Span scan = metadata;
        size_t foundDrivers = 0;
        while (scan.Size() > 0)
        {
            auto test = FindByGuid(scan, manifestHdrGuid);
            if (test == nullptr)
                break;
            foundDrivers++;
            scan = scan.Subspan(sizeof(npk_driver_manifest), -1ul);
        }
        
        //a module with 0 drivers has no purpose to us, abort here.
        VALIDATE_(foundDrivers > 0, false);

        scan = metadata;
        while (scan.Size() > 0)
        {
            auto apiManifest = static_cast<const npk_driver_manifest*>(FindByGuid(scan, manifestHdrGuid));
            scan = scan.Subspan(sizeof(npk_driver_manifest), -1ul);
            if (apiManifest == nullptr)
                break;

            //TODO: stash located drivers, and maybe a hash of the blob so we can rescan if necessary.
            Log("Module \"%s\" has driver: %s v%u.%u.%u, loadtype=%s", LogLevel::Verbose,
                filepath.Begin(), apiManifest->friendly_name, apiManifest->ver_major,
                apiManifest->ver_minor, apiManifest->ver_rev, LoadTypeStrs[(size_t)apiManifest->load_type]);
        }

        return true;
    }

    void ScanForModules(sl::StringSpan directory)
    {
        Log("Scanning for kernel modules in \"%s\"", LogLevel::Info, directory.Begin());

        using namespace Filesystem;
        auto dir = VfsLookup(directory, KernelFsCtxt);
        VALIDATE_(dir.Valid(),);
        VALIDATE_(dir->type == NodeType::Directory,);

        size_t found = 0;
        for (size_t i = 0; true; i++)
        {
            auto child = dir->GetChild(i, KernelFsCtxt);
            if (!child.Valid())
                break;
            if (child->type != NodeType::File)
                continue;

            NodeProps props;
            if (!child->GetProps(props, KernelFsCtxt))
                continue;

            sl::String filepath = sl::String(directory);
            if (!filepath.EndsWith('/'))
                filepath += '/';
            filepath += props.name;
            if (ScanForDrivers(filepath.Span()))
                found++;
        }
        Log("Detected %lu kernel modules in \"%s\"", LogLevel::Verbose, found, directory.Begin());
    }
}
