#include <filesystem/InitDisk.h>
#include <filesystem/Filesystem.h>
#include <filesystem/TempFs.h>
#include <debug/Log.h>
#include <drivers/DriverManager.h>
#include <interfaces/loader/Generic.h>
#include <memory/VmObject.h>
#include <formats/Tar.h>
#include <formats/Url.h>
#include <Memory.h>
#include <UnitConverter.h>

namespace Npk::Filesystem
{
    void LoadInitdiskFile(npk_filesystem_device_api* fsApi, const sl::TarHeader* file)
    {
        const auto conv = sl::ConvertUnits(file->SizeBytes());
        Log("Loading initdisk file: %s (%zu.%zu %sB)", LogLevel::Verbose,
            file->Filename().Begin(), conv.major, conv.minor, conv.prefix);

        const sl::Url path = sl::Url::Parse(file->Filename());
        sl::StringSpan segment = path.GetNextSeg();
        VfsId parent = { .driverId = fsApi->header.id, .vnodeId = fsApi->get_root(&fsApi->header) };

        while (!path.GetNextSeg(segment).Empty())
        {
            auto child = VfsFindChild(parent, segment);
            if (!child.HasValue())
                child = VfsCreate(parent, NodeType::Directory, segment);
            VALIDATE_(child.HasValue(), );

            parent = *child;
            segment = path.GetNextSeg(segment);
        }

        auto fileId = VfsCreate(parent, NodeType::File, segment);
        VALIDATE_(fileId.HasValue(), );

        NodeAttribs attribs {};
        attribs.size = file->SizeBytes();
        VALIDATE_(VfsSetAttribs(*fileId, attribs, NodeAttribFlag::Size), );
        
        VmFileArg vmoArg {};
        vmoArg.id = *fileId;
        vmoArg.noDeferBacking = true;

        VmObject fileVmo(file->SizeBytes(), vmoArg, VmFlag::File | VmFlag::Write);
        VALIDATE_(fileVmo.Valid(), );
        sl::memcopy(file->Data(), fileVmo->ptr, file->SizeBytes());
        //TODO: migrate initdisk pages to filecache, instead of copying
    }

    void TryLoadInitdisk()
    {
        auto module = GetInitdisk();
        if (module.Size() == 0)
        {
            Log("Failed to load initdisk, no bootloader modules present", LogLevel::Warning);
            return;
        }
        Log("Loading initdisk from module at %p", LogLevel::Info, module.Begin());

        //create a mountpoint for ourselves ("/initdisk/") and mount a tempFS there.
        auto maybeRoot = VfsLookup("/");
        VALIDATE_(maybeRoot.HasValue(),); //if this ever fails.. I dunno man.
        auto maybeMountpoint = VfsCreate(*maybeRoot, NodeType::Directory, "initdisk");
        VALIDATE_(maybeMountpoint.HasValue(),);

        const size_t tempFsId = CreateTempFs("initdisk");
        const MountOptions options
        {
            .writable = true,
            .uncachable = false
        };
        VALIDATE_(VfsMount(*maybeMountpoint, tempFsId, options), );

        auto tempFs = Drivers::DriverManager::Global().GetApi(tempFsId);
        VALIDATE_(tempFs.Valid(), );
        auto* fsApi = reinterpret_cast<npk_filesystem_device_api*>(tempFs->api);

        const sl::TarHeader* scan = static_cast<sl::TarHeader*>(static_cast<void*>(module.Begin()));
        while ((uintptr_t)scan < (uintptr_t)module.End())
        {
            if (scan->IsZero() && scan->Next()->IsZero())
                break; //two zero sectors in a row indicates an end to the archive's data.

            if (scan->Type() != sl::TarEntryType::File)
            {
                scan = scan->Next();
                continue;
            }

            if ((uintptr_t)scan + scan->SizeBytes() > (uintptr_t)module.End())
            {
                Log("Initdisk file %s extends beyond bootloader module limits", LogLevel::Error,
                    scan->Filename().Begin());
                break;
            }
            LoadInitdiskFile(fsApi, scan);
            scan = scan->Next();
        }

        Log("Initdisk files populated, mounted at \"/initdisk/\"", LogLevel::Info);
        PrintNode(*VfsLookup("/"), 0);
    }
}
