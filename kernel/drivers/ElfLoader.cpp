#include <drivers/ElfLoader.h>
#include <drivers/DriverManager.h>
#include <debug/Log.h>
#include <debug/Symbols.h>
#include <filesystem/Filesystem.h>
#include <interfaces/driver/Api.h>
#include <Memory.h>

namespace Npk::Drivers
{
    constexpr sl::Elf_Word NpkPhdrMagic = 0x6E706B6D;

    static sl::StringSpan GetShortName(sl::StringSpan fullname)
    {
        while (true)
        {
            const size_t index = sl::memfirst(fullname.Begin(), '/', fullname.Size());
            if (index == fullname.Size())
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

        VmFileArg vmoArg {};
        vmoArg.filepath = filepath;
        VmObject file(attribs->size, reinterpret_cast<uintptr_t>(&vmoArg), VmFlag::File);
        VALIDATE_(file.Valid(), {});

        VALIDATE_(sl::ValidateElfHeader(file->ptr, sl::ET_DYN), {});
        return file;
    }

    static bool ApplyRelocation(const DynamicElfInfo& dynInfo, const void* reloc, bool isRela, uintptr_t base, bool allowLookup)
    {
        auto rela = static_cast<const sl::Elf_Rela*>(reloc);
        const auto type = ELF_R_TYPE(rela->r_info);
        const auto b = base;
        const auto p = base + rela->r_offset;
        const uintptr_t s = [&]() 
        {
            auto sym = dynInfo.symTable[ELF_R_SYM(rela->r_info)];
            if (sym.st_value != 0 || !allowLookup || sym.st_name == 0)
                return sym.st_value;

            using namespace Debug;
            auto symName = dynInfo.strTable + sym.st_name;
            const size_t nameLen = sl::memfirst(symName, 0, 0);
            auto resolved = SymbolFromName({ symName, nameLen }, SymbolFlag::Public | SymbolFlag::Kernel);
            return resolved.HasValue() ? resolved->base : 0;
        }();

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
        if (computed.usedSymbol && s == 0)
            return false;
        VALIDATE(computed.length != 0, false, "Unknown relocation type");
        sl::memcopy(&computed.value, reinterpret_cast<void*>(p), computed.length);
        return true;
    }

    static bool DoRelocations(const DynamicElfInfo& dynInfo, VmObject& buffer, uintptr_t offset, size_t limit, bool isDriver)
    {
        size_t failedRelocs = 0;

        for (size_t i = 0; i < dynInfo.relaCount; i++)
        {
            auto rel = dynInfo.relaEntries[i];
            if (rel.r_offset < offset || rel.r_offset > offset + limit)
                continue;

            if (!ApplyRelocation(dynInfo, &rel, true, buffer->raw - offset, isDriver))
            {
                failedRelocs++;
                Log("Rela %lu failed: r_offset(+base)=0x%lx, r_info=0x%lx", LogLevel::Error,
                    i, rel.r_offset + (buffer->raw - offset), rel.r_info);
            }
        }

        for (size_t i = 0; i < dynInfo.relCount; i++)
        {
            auto rel = dynInfo.relEntries[i];
            if (rel.r_offset < offset || rel.r_offset > offset + limit)
                continue;

            if (!ApplyRelocation(dynInfo, &rel, false, buffer->raw - offset, isDriver))
            {
                failedRelocs++;
                Log("Rel %lu failed: r_offset(+base)=0x%lx, r_info=0x%lx", LogLevel::Error,
                    i, rel.r_offset + (buffer->raw - offset), rel.r_info);
            }
        }

        return failedRelocs == 0;
    }

    static bool LinkPlt(const DynamicElfInfo& dynInfo, VmObject& buffer, bool isDriver)
    {
        const size_t offsetIncrement = dynInfo.pltUsesRela ? sizeof(sl::Elf_Rela) : sizeof(sl::Elf_Rel);
        size_t failedRelocs = 0;

        for (size_t offset = 0; offset < dynInfo.pltRelocsSize; offset += offsetIncrement)
        {
            auto rel = sl::CNativePtr(dynInfo.pltRelocs).Offset(offset).As<const sl::Elf_Rel>();

            if (!ApplyRelocation(dynInfo, rel, dynInfo.pltUsesRela, buffer->raw, isDriver))
            {
                Log("PltRel %lu failed: r_offset(+base)=0x%lx, r_info=0x%lx", LogLevel::Error,
                    offset / offsetIncrement, rel->r_offset + buffer->raw, rel->r_info);
                failedRelocs++;
            }
        }

        return failedRelocs == 0;
    }

    static sl::Opt<DynamicElfInfo> ParseDynamic(VmObject& file, uintptr_t loadBase)
    {
        auto ehdr = file->As<const sl::Elf_Ehdr>();
        auto phdrs = file->As<const sl::Elf_Phdr>(ehdr->e_phoff);

        const sl::Elf_Dyn* dyn = nullptr;
        for (size_t i = 0; i < ehdr->e_phnum; i++)
        {
            if (phdrs[i].p_type != sl::PT_DYNAMIC)
                continue;

            auto dynamicHdr = phdrs + i;
            dyn = file->As<const sl::Elf_Dyn>(dynamicHdr->p_offset);
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
                info.relEntries = reinterpret_cast<const sl::Elf_Rel*>(dyn[i].d_ptr + loadBase);
                break;
            case sl::DT_RELSZ:
                info.relCount = dyn[i].d_ptr / sizeof(sl::Elf_Rel);
                break;
            case sl::DT_RELA:
                info.relaEntries = reinterpret_cast<const sl::Elf_Rela*>(dyn[i].d_ptr + loadBase);
                break;
            case sl::DT_RELASZ:
                info.relaCount = dyn[i].d_ptr / sizeof(sl::Elf_Rela);
                break;
            case sl::DT_STRTAB:
                info.strTable = reinterpret_cast<const char*>(dyn[i].d_ptr + loadBase);
                break;
            case sl::DT_SYMTAB:
                info.symTable = reinterpret_cast<const sl::Elf_Sym*>(dyn[i].d_ptr + loadBase);
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

    void ScanForModules(sl::StringSpan dirpath)
    {
        Log("Scanning for kernel modules in \"%s\"", LogLevel::Verbose, dirpath.Begin());

        using namespace Filesystem;
        auto dirId = VfsLookup(dirpath);
        VALIDATE_(dirId.HasValue(),);

        auto dirList = VfsReadDir(*dirId);
        VALIDATE_(dirList.HasValue(), );
        
        size_t found = 0;
        for (size_t i = 0; i < dirList->children.Size(); i++)
        {
            auto attribs = VfsGetAttribs(dirList->children[i].id);
            if (!attribs.HasValue())
                continue;
            if (attribs->type != NodeType::File)
                continue;

            const sl::String filepath = VfsGetPath(dirList->children[i].id);
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

        const VmObject metadataVmo = [&]() -> const VmObject
        {
            auto ehdr = file->As<const sl::Elf_Ehdr>();
            auto phdrs = sl::FindPhdrs(ehdr, NpkPhdrMagic);
            VALIDATE_(phdrs.Size() == 1,{});

            VmObject workBuffer(phdrs[0]->p_memsz, 0, VmFlag::Anon | VmFlag::Write);
            sl::memcopy(file->As<void>(phdrs[0]->p_offset), workBuffer->ptr, phdrs[0]->p_filesz);
            sl::memset(workBuffer->As<void>(phdrs[0]->p_filesz), 0, phdrs[0]->p_memsz - phdrs[0]->p_filesz);

            auto dynInfo = ParseDynamic(file, file->raw);
            VALIDATE_(dynInfo.HasValue(), {});
            //we pass isDriver=false here because we're not actually running the code, so resolving
            //external symbols is a waste of time here - even if its technically incorrect.
            VALIDATE_(DoRelocations(*dynInfo, workBuffer, phdrs[0]->p_vaddr, phdrs[0]->p_memsz, false), {});
            return workBuffer;
        }();
        VALIDATE_(metadataVmo.Valid(), false);

        const uint8_t manifestHdrGuid[] = NP_MODULE_MANIFEST_GUID;
        size_t usableDrivers = 0;
        auto scan = metadataVmo.ConstSpan();
        while (scan.Size() > 0)
        {
            auto apiManifest = static_cast<const npk_driver_manifest*>(FindGuid(scan, manifestHdrGuid));
            if (apiManifest == nullptr)
                break;
            const size_t offset = (uintptr_t)apiManifest - (uintptr_t)scan.Begin();
            scan = scan.Subspan(offset + sizeof(npk_driver_manifest), -1ul);

            if (apiManifest->api_ver_major != NP_MODULE_API_VER_MAJOR
                || apiManifest->api_ver_minor > NP_MODULE_API_VER_MINOR)
            {
                Log("Module %s provides unusable driver %s, api version mismatch (kernel: %u.%u, driver: %u.%u)",
                    LogLevel::Error, shortName.Begin(), apiManifest->friendly_name, 
                    NP_MODULE_API_VER_MAJOR, NP_MODULE_API_VER_MINOR, apiManifest->api_ver_major,
                    apiManifest->api_ver_minor);
                continue;
            }

            //TODO: TOCTOU-style bug potential here, what if a file's content changes
            //after this point but before the driver is loaded? Probably should store
            //a file hash with the cached data, so we can verify its validity later.
            DriverManifest* manifest = new DriverManifest();
            manifest->references = 0;
            manifest->friendlyName = sl::String(apiManifest->friendly_name);
            manifest->sourcePath = filepath;

            sl::Vector<npk_load_name> loadNames(apiManifest->load_name_count);

            for (size_t i = 0; i < loadNames.Capacity(); i++)
            {
                auto& name = loadNames.EmplaceBack();
                name.type = apiManifest->load_names[i].type;
                name.length = apiManifest->load_names[i].length;

                uint8_t* buffer = new uint8_t[name.length];
                sl::memcopy(apiManifest->load_names[i].str, buffer, name.length);
                name.str = buffer;
            }
            manifest->loadNames = sl::Move(loadNames);

            usableDrivers++;
            DriverManager::Global().AddManifest(manifest);
            (void)manifest;
            Log("Module %s provides driver: %s v%u.%u", LogLevel::Info, shortName.Begin(),
                manifest->friendlyName.C_Str(), apiManifest->ver_major, apiManifest->ver_minor);
        }

        return usableDrivers > 0;
    }

    sl::Handle<LoadedElf> LoadElf(VMM* vmm, sl::StringSpan filepath, LoadingDriverInfo* driverInfo)
    {
        const auto shortName = GetShortName(filepath);
        VmObject file = OpenElf(filepath);
        VALIDATE_(file.Valid(), {});

        //get access to the elf header and program headers
        auto ehdr = file->As<const sl::Elf_Ehdr>();
        sl::Span<const sl::Elf_Phdr> phdrs { file->As<const sl::Elf_Phdr>(ehdr->e_phoff), ehdr->e_phnum };

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
                baseMemoryAddr = phdrs[i].p_vaddr;
            const size_t localMax = sl::AlignUp(phdrs[i].p_vaddr + phdrs[i].p_memsz, phdrs[i].p_align);
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

        const auto dynInfo = ParseDynamic(file, vmo->raw);
        VALIDATE_(dynInfo.HasValue(), {});
        VALIDATE_(DoRelocations(*dynInfo, vmo, 0, -1ul, driverInfo != nullptr), {});
        VALIDATE_(LinkPlt(*dynInfo, vmo, driverInfo != nullptr), {});

        auto moduleManifest = [&]() -> const npk_driver_manifest*
        {
            const auto phdrs = sl::FindPhdrs(ehdr, NpkPhdrMagic);
            if (phdrs.Size() != 1)
                return nullptr;
            const auto phdr = phdrs[0];

            const uint8_t manifestHdrGuid[] = NP_MODULE_MANIFEST_GUID;
            auto scan = vmo.ConstSpan().Subspan(phdr->p_vaddr, phdr->p_filesz);
            while (!scan.Empty())
            {
                auto apiManifest = static_cast<const npk_driver_manifest*>(FindGuid(scan, manifestHdrGuid));
                if (apiManifest == nullptr)
                    return nullptr;
                const size_t offset = (uintptr_t)apiManifest - (uintptr_t)scan.Begin();
                scan = scan.Subspan(offset, -1ul);

                sl::StringSpan apiName(apiManifest->friendly_name, sl::memfirst(apiManifest->friendly_name, 0, 0));
                if (driverInfo->name == apiName)
                    return apiManifest;
            }

            return nullptr;
        }();
        if (driverInfo != nullptr)
            VALIDATE_(moduleManifest != nullptr, {});

        sl::Vector<VmObject> finalVmos;
        for (size_t i = 0; i < phdrs.Size(); i++)
        {
            if (phdrs[i].p_type != sl::PT_LOAD)
                continue;
            VALIDATE(vmo.Valid(), {}, "Bad alignment in ELF PHDR");

            size_t split = sl::AlignUp(phdrs[i].p_vaddr + phdrs[i].p_memsz, phdrs[i].p_align);
            split -= sl::AlignDown(phdrs[i].p_vaddr, phdrs[i].p_align);
            VmObject& localVmo = finalVmos.EmplaceBack(sl::Move(vmo.Subdivide(split, true)));
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

        if (moduleManifest != nullptr)
        {
            //get the entry point from the module metadata
            elfInfo->symbolRepo = Debug::LoadElfModuleSymbols(shortName, file, loadBase);

            //driver manager needs access to the manifest to set up the driver control block, also
            //do some quick validation.
            VALIDATE_(moduleManifest->process_event != nullptr, {});
            driverInfo->manifest = moduleManifest;
        }

        return sl::Handle(elfInfo);
    }
}
