#include <drivers/ElfLoader.h>
#include <drivers/DriverManager.h>
#include <drivers/api/Api.h>
#include <debug/Log.h>
#include <debug/Symbols.h>
#include <filesystem/Filesystem.h>
#include <Memory.h>

namespace Npk::Drivers
{
    static sl::StringSpan GetShortName(sl::StringSpan fullname)
    {
        while (true)
        {
            const size_t index = sl::memfirst(fullname.Begin(), '/', fullname.Size());
            if (index == -1ul)
                break;
            fullname = fullname.Subspan(index + 1, -1ul);
        }

        return fullname;
    }

    //TOOD: move to syslib with other GUID util functions
    static const void* FindGuid(sl::Span<const uint8_t> data, sl::Span<const uint8_t> guid)
    {
        if (data.Size() == 0)
            return nullptr;

        for (size_t i = 0; i < data.Size() - guid.Size(); i++)
        {
            if (data[i] != guid[0])
                continue;
            if (data.Subspan(i, guid.Size()) == guid)
                return static_cast<const void*>(data.Begin() + i);
        }

        return nullptr;
    }

    static VmObject OpenElf(sl::StringSpan filepath)
    {
        auto fileId = Filesystem::VfsLookup(filepath);
        VALIDATE_(fileId.HasValue(), {});
        auto attribs = Filesystem::VfsGetAttribs(*fileId);
        VALIDATE_(attribs.HasValue(), {});
        VALIDATE_(attribs->type == Filesystem::NodeType::File, {});

        Memory::VmoFileInitArg vmoArg {};
        vmoArg.filepath = filepath;
        VmObject file(attribs->size, reinterpret_cast<uintptr_t>(&vmoArg), VmFlag::File);
        VALIDATE_(file.Valid(), {});

        VALIDATE_(sl::ValidateElfHeader(file->ptr, sl::ET_DYN), {});
        return file;
    }

    static bool ApplyRelocation(const DynamicElfInfo& dynInfo, const void* reloc, bool isRela, uintptr_t base, uintptr_t s = 0)
    {
        auto rela = static_cast<const sl::Elf64_Rela*>(reloc);
        const auto type = ELF64_R_TYPE(rela->r_info);
        const auto b = base;
        const auto p = base + rela->r_offset;

        if (s == 0)
            s = dynInfo.symTable[ELF64_R_SYM(rela->r_info)].st_value;

        uintptr_t a = 0;
        if (isRela)
            a = rela->r_addend;
        else
        {
            const auto opLength = sl::ComputeRelocation(type, 0, b, s, p);
            VALIDATE(opLength.length != 0, false, "Unknown relocation type");
            VALIDATE_(opLength.length <= sizeof(uintptr_t), false);
            sl::memcopy(reinterpret_cast<void*>(p), &a, opLength.length);
        }

        const auto computed = sl::ComputeRelocation(type, a, b, s, p);
        VALIDATE(computed.length != 0, false, "Unknown relocation type");
        sl::memcopy(&computed.value, reinterpret_cast<void*>(p), computed.length);
        return true;
    }

    static bool DoRelocations(const DynamicElfInfo& dynInfo, VmObject& buffer, uintptr_t offset, size_t limit)
    {
        for (size_t i = 0; i < dynInfo.relaCount; i++)
        {
            auto rel = dynInfo.relaEntries[i];
            if (rel.r_offset < offset || rel.r_offset > offset + limit)
                continue;

            VALIDATE_(ApplyRelocation(dynInfo, &rel, true, buffer->raw - offset), false);
        }

        for (size_t i = 0; i < dynInfo.relCount; i++)
        {
            auto rel = dynInfo.relEntries[i];
            if (rel.r_offset < offset || rel.r_offset > offset + limit)
                continue;

            VALIDATE_(ApplyRelocation(dynInfo, &rel, false, buffer->raw - offset), false);
        }

        return true;
    }

    static bool LinkPlt(const DynamicElfInfo& dynInfo, VmObject& buffer)
    {
        const size_t offsetIncrement = dynInfo.pltUsesRela ? sizeof(sl::Elf64_Rela) : sizeof(sl::Elf64_Rel);

        for (size_t offset = 0; offset < dynInfo.pltRelocsSize; offset += offsetIncrement)
        {
            auto rel = sl::NativePtr(dynInfo.pltRelocs).Offset(offset).As<const sl::Elf64_Rel>();
            auto symbolName = dynInfo.strTable + dynInfo.symTable[ELF64_R_SYM(rel->r_info)].st_name;
            const size_t nameLen = sl::memfirst(symbolName, 0, 0);

            using namespace Debug;
            auto resolved = SymbolFromName({ symbolName, nameLen }, SymbolFlag::Public | SymbolFlag::Kernel);
            if (!resolved.HasValue())
            {
                Log("Failed to resolve PLT symbol: %s", LogLevel::Error, symbolName);
                return false;
            }

            VALIDATE_(ApplyRelocation(dynInfo, rel, dynInfo.pltUsesRela, buffer->raw, resolved->base), false);
        }

        return true;
    }

    static sl::Opt<DynamicElfInfo> ParseDynamic(VmObject& file, uintptr_t loadBase)
    {
        auto ehdr = file->As<const sl::Elf64_Ehdr>();
        auto phdrs = file->As<const sl::Elf64_Phdr>(ehdr->e_phoff);

        const sl::Elf64_Dyn* dyn = nullptr;
        for (size_t i = 0; i < ehdr->e_phnum; i++)
        {
            if (phdrs[i].p_type != sl::PT_DYNAMIC)
                continue;

            auto dynamicHdr = phdrs + i;
            dyn = file->As<const sl::Elf64_Dyn>(dynamicHdr->p_offset);
            break;
        }
        VALIDATE_(dyn != nullptr, {});

        DynamicElfInfo info {};
        for (size_t i = 0; dyn[i].d_tag != sl::DT_NULL; i++)
        {
            switch (dyn[i].d_tag)
            {
            case sl::DT_NEEDED:
                return {}; //we dont support additional libraries in the kernel loader
            case sl::DT_REL:
                info.relEntries = reinterpret_cast<const sl::Elf64_Rel*>(dyn[i].d_ptr + loadBase);
                break;
            case sl::DT_RELSZ:
                info.relCount = dyn[i].d_ptr / sizeof(sl::Elf64_Rel);
                break;
            case sl::DT_RELA:
                info.relaEntries = reinterpret_cast<const sl::Elf64_Rela*>(dyn[i].d_ptr + loadBase);
                break;
            case sl::DT_RELASZ:
                info.relaCount = dyn[i].d_ptr / sizeof(sl::Elf64_Rela);
                break;
            case sl::DT_STRTAB:
                info.strTable = reinterpret_cast<const char*>(dyn[i].d_ptr + loadBase);
                break;
            case sl::DT_SYMTAB:
                info.symTable = reinterpret_cast<const sl::Elf64_Sym*>(dyn[i].d_ptr + loadBase);
                break;
            case sl::DT_JMPREL:
                info.pltRelocs = reinterpret_cast<const void*>(dyn[i].d_ptr + loadBase);
                break;
            case sl::DT_PLTREL:
                info.pltUsesRela = (dyn[i].d_val == sl::DT_RELA);
                break;
            case sl::DT_PLTRELSZ:
                info.pltRelocsSize = dyn[i].d_val;
                break;
            }
        }

        return info;
    }

    static const VmObject LoadMetadataSection(VmObject& file)
    {
        auto ehdr = file->As<const sl::Elf64_Ehdr>();
        auto metadataShdr = sl::FindShdr(ehdr, ".npk_module");
        VALIDATE_(metadataShdr != nullptr, {});

        //TODO: small misalignment here I think, we use the base of the working buffer which becomes page-aligned
        VmObject workingBuffer(metadataShdr->sh_size, 0, VmFlag::Anon | VmFlag::Write);
        sl::memcopy(file->As<void>(metadataShdr->sh_offset), workingBuffer->ptr, metadataShdr->sh_size);

        auto dynInfo = ParseDynamic(file, file->raw);
        VALIDATE_(dynInfo.HasValue(), {});
        VALIDATE_(DoRelocations(*dynInfo, workingBuffer, metadataShdr->sh_addr, metadataShdr->sh_size), {});
        return workingBuffer;
    }

    void ScanForModules(sl::StringSpan dirpath)
    {
        Log("Scanning for kernel modules in \"%s\"", LogLevel::Verbose, dirpath.Begin());

        using namespace Filesystem;
        auto dirId = VfsLookup(dirpath);
        VALIDATE_(dirId.HasValue(),);

        auto dirList = VfsGetDirListing(*dirId);
        VALIDATE_(dirList.HasValue(), );
        
        size_t found = 0;
        for (size_t i = 0; i < dirList->children.Size(); i++)
        {
            auto child = VfsGetNode(dirList->children[i]);
            if (!child.Valid())
                continue;
            if (child->type != NodeType::File)
                continue;

            const sl::String filepath = VfsGetPath(dirList->children[i]);
            if (ScanForDrivers(filepath.Span()))
                found++;
        }

        Log("Detected %lu kernel modules in \"%s\"", LogLevel::Verbose, found, dirpath.Begin());
    }

    bool ScanForDrivers(sl::StringSpan filepath)
    {
        const auto shortName = GetShortName(filepath);
        VmObject file = OpenElf(filepath);
        VALIDATE_(file.Valid(), false);

        VmObject metadataVmo = LoadMetadataSection(file); //TODO: remove this function (its only used in one place)
        VALIDATE_(metadataVmo.Valid(), false);

        const uint8_t moduleHdrGuid[] = NP_MODULE_META_START_GUID;
        const uint8_t manifestHdrGuid[] = NP_MODULE_MANIFEST_GUID;
        auto moduleInfo = static_cast<const npk_module_metadata*>(FindGuid(metadataVmo.ConstSpan(), moduleHdrGuid));
        VALIDATE_(moduleInfo != nullptr, false);

        //API compatibility: major version must match, and kernel minor version must be greater/equal
        VALIDATE_(moduleInfo->api_ver_major == NP_MODULE_API_VER_MAJOR, false);
        VALIDATE_(moduleInfo->api_ver_minor <= NP_MODULE_API_VER_MINOR, false);

        auto scan = metadataVmo.ConstSpan();
        while (scan.Size() > 0)
        {
            auto apiManifest = static_cast<const npk_driver_manifest*>(FindGuid(scan, manifestHdrGuid));
            if (apiManifest == nullptr)
                break;
            const size_t offset = (uintptr_t)apiManifest - (uintptr_t)scan.Begin();
            scan = scan.Subspan(offset + sizeof(npk_driver_manifest), -1ul);

            //TODO: TOCTOU-style bug potential here, what if a file's content changes
            //after this point but before the driver is loaded? Probably should store
            //a file hash with the cached data, so we can verify its validity later.
            DriverManifest* manifest = new DriverManifest();
            manifest->references = 0;
            manifest->friendlyName = sl::String(apiManifest->friendly_name);
            manifest->sourcePath = filepath;
            manifest->loadType = static_cast<LoadType>(apiManifest->load_type);

            uint8_t* loadString = new uint8_t[apiManifest->load_str_len];
            sl::memcopy(apiManifest->load_str, loadString, apiManifest->load_str_len);
            manifest->loadStr = sl::Span<const uint8_t>(loadString, apiManifest->load_str_len);
            
            DriverManager::Global().AddManifest(manifest);
            (void)manifest;
            Log("Module %s provides driver: %s v%u.%u.%u", LogLevel::Info, shortName.Begin(),
                manifest->friendlyName.C_Str(), apiManifest->ver_major, apiManifest->ver_minor,
                apiManifest->ver_rev);
        }
        return true;
    }

    static const npk_module_metadata* GetElfModuleMetadata(VmObject& file, VmObject& loaded)
    {
        auto metadataShdr = sl::FindShdr(file->As<const sl::Elf64_Ehdr>(), ".npk_module");
        if (metadataShdr == nullptr)
            return nullptr;
        if (metadataShdr->sh_addr >= loaded.Size())
            return nullptr;

        const uint8_t moduleHdrGuid[] = NP_MODULE_META_START_GUID;
        auto shdrSpan = loaded.ConstSpan().Subspan(metadataShdr->sh_addr, metadataShdr->sh_size);
        auto moduleInfo = static_cast<const npk_module_metadata*>(FindGuid(shdrSpan, moduleHdrGuid));

        if (moduleInfo == nullptr)
            return nullptr;
        if (moduleInfo->api_ver_major != NP_MODULE_API_VER_MAJOR)
            return nullptr;
        if (moduleInfo->api_ver_minor > NP_MODULE_API_VER_MINOR)
            return nullptr;

        return moduleInfo;
    }

    static const npk_driver_manifest* GetElfModuleManifest(VmObject& file, VmObject& loaded, sl::StringSpan name)
    {
        auto metadataShdr = sl::FindShdr(file->As<const sl::Elf64_Ehdr>(), ".npk_module");
        if (metadataShdr == nullptr)
            return nullptr;
        if (metadataShdr->sh_addr >= loaded.Size())
            return nullptr;

        const uint8_t manifestHdrGuid[] = NP_MODULE_MANIFEST_GUID;
        auto shdrSpan = loaded.ConstSpan().Subspan(metadataShdr->sh_addr, metadataShdr->sh_size);

        while (!shdrSpan.Empty())
        {
            auto manifest = static_cast<const npk_driver_manifest*>(FindGuid(shdrSpan, manifestHdrGuid));
            if (manifest == nullptr)
                break;
            const size_t offset = (uintptr_t)manifest - (uintptr_t)shdrSpan.Begin();
            shdrSpan = shdrSpan.Subspan(offset + sizeof(npk_driver_manifest), -1ul);

            sl::StringSpan manifestName(manifest->friendly_name, sl::memfirst(manifest->friendly_name, 0, 0));
            if (manifestName == name)
                return manifest;
        }

        return nullptr;
    }

    sl::Handle<LoadedElf> LoadElf(VMM* vmm, sl::StringSpan filepath, LoadingDriverInfo* driverInfo)
    {
        const auto shortName = GetShortName(filepath);
        VmObject file = OpenElf(filepath);
        VALIDATE_(file.Valid(), {});

        //get access to the elf header and program headers
        auto ehdr = file->As<const sl::Elf64_Ehdr>();
        sl::Span<const sl::Elf64_Phdr> phdrs { file->As<const sl::Elf64_Phdr>(ehdr->e_phoff), ehdr->e_phnum };

        /* Shared objects (PIE executables are basically just a shared object with an entry point),
         * can be loaded at any address, but the relationship between the PHDRs must be the same
         * as it is in the file. They're also usually linked at address 0. To load an SO, we calculate
         * the highest address of a loadable phdr, allocate a single block of virtual memory of this
         * size. Now we can load the phdrs and later subdivide the memory block when we want to apply
         * individual permissions to each phdr.
         */
        uintptr_t baseMemoryAddr = -1ul;
        uintptr_t maxMemoryAddr = 0;
        for (size_t i = 0; i < phdrs.Size(); i++)
        {
            if (phdrs[i].p_type != sl::PT_LOAD)
                continue;

            if (phdrs[i].p_vaddr < baseMemoryAddr)
                baseMemoryAddr = 0;
            const size_t localMax = phdrs[i].p_vaddr + phdrs[i].p_memsz;
            if (localMax > maxMemoryAddr)
                maxMemoryAddr = localMax;
        }
        VALIDATE_(baseMemoryAddr == 0, {});

        //TODO: we should check the given VMM is actually active, because we write to these VMOs
        VmObject vmo(vmm, maxMemoryAddr, 0, VmFlag::Anon | VmFlag::Write);
        VALIDATE_(vmo.Valid(), {});
        const uintptr_t loadBase = vmo->raw;
        Log("Loading elf %s with base address of 0x%lx", LogLevel::Verbose, 
            shortName.Begin(), loadBase);

        for (size_t i = 0; i < phdrs.Size(); i++)
        {
            if (phdrs[i].p_type != sl::PT_LOAD)
                continue;

            sl::memcopy(file->As<void>(phdrs[i].p_offset), vmo->As<void>(phdrs[i].p_vaddr), phdrs[i].p_filesz);
            const size_t zeroes = phdrs[i].p_memsz - phdrs[i].p_filesz;
            if (zeroes != 0)
                sl::memset(vmo->As<void>(phdrs[i].p_vaddr + phdrs[i].p_filesz), 0, zeroes);

            Log("Loaded elf %s phdr %lu at 0x%lx (+0x%lx zeroes)", LogLevel::Verbose,
                shortName.Begin(), i, loadBase + phdrs[i].p_vaddr, zeroes);
        }

        auto dynInfo = ParseDynamic(file, vmo->raw);
        VALIDATE_(dynInfo.HasValue(), {});
        VALIDATE_(DoRelocations(*dynInfo, vmo, 0, -1ul), {});

        const npk_module_metadata* moduleMetadata = nullptr;
        const npk_driver_manifest* moduleManifest = nullptr;
        if (driverInfo != nullptr)
        {
            moduleMetadata = GetElfModuleMetadata(file, vmo);
            moduleManifest = GetElfModuleManifest(file, vmo, driverInfo->name);
            if (moduleMetadata == nullptr || moduleManifest == nullptr)
            {
                Log("Failed to load module %s, missing metadata header or driver manifest",
                    LogLevel::Error, shortName.Begin());
                return {};
            }

            //for kernel modules we'll also bind PLT functions to kernel functions, so
            //that modules can call info the kernel via the driver API.
            VALIDATE_(LinkPlt(*dynInfo, vmo), {});
        }

        sl::Vector<VmObject> finalVmos;
        for (size_t i = 0; i < phdrs.Size(); i++)
        {
            if (phdrs[i].p_type != sl::PT_LOAD)
                continue;
            VALIDATE(vmo.Valid(), {}, "Bad alignment in ELF PHDR");

            VmObject& localVmo = finalVmos.EmplaceBack(sl::Move(vmo.Subdivide(phdrs[i].p_memsz, true)));
            VALIDATE_(localVmo.Valid(), {});

            VmFlags flags {};
            if (phdrs[i].p_flags & sl::PF_W)
                flags |= VmFlag::Write;
            if (phdrs[i].p_flags & sl::PF_X)
                flags |= VmFlag::Execute;
            const auto readback = localVmo.Flags(flags);
            VALIDATE_((readback & flags) == flags, {});
        }

        LoadedElf* elfInfo = new LoadedElf();
        elfInfo->loadBase = loadBase;
        elfInfo->segments = sl::Move(finalVmos);
        elfInfo->references = 0;
        elfInfo->entryAddr = ehdr->e_entry + loadBase;

        if (moduleMetadata != nullptr)
        {
            //get the entry point from the module metadata
            elfInfo->entryAddr = reinterpret_cast<uintptr_t>(moduleManifest->entry);
            //store the module's symbols in the global symbol storage
            elfInfo->symbolRepo = Debug::LoadElfModuleSymbols(shortName, file, loadBase);

            //driver manager needs access to the manifest to set up the driver control block, also
            //do some quick validation.
            VALIDATE_(moduleManifest->process_event != nullptr, {});
            driverInfo->manifest = moduleManifest;
        }

        return sl::Handle(elfInfo);
    }
}
