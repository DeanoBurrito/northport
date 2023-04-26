#include <filesystem/Vfs.h>
#include <boot/LimineTags.h>
#include <debug/Log.h>
#include <filesystem/TempFs.h>
#include <containers/Vector.h>
#include <Memory.h>

namespace Npk::Filesystem
{
    sl::Vector<Vfs*> filesystems;
    sl::RwLock filesystemsLock;
    
    void InitVfs()
    {
        const bool noRootFs = true; //TODO: set this if no config data for mounting the root fs is found.
        
        filesystemsLock.WriterLock();
        if (noRootFs)
        {
            filesystems.EmplaceBack(new TempFs());
            Log("VFS initialized: No root filesystem found, using tempfs.", LogLevel::Info);
        }
        else
            ASSERT_UNREACHABLE()

        //scan through any bootloader modules, see if there's an initdisk to mount.
        if (Boot::modulesRequest.response != nullptr)
        {
            const auto* resp = Boot::modulesRequest.response;
            for (size_t i = 0; i < resp->module_count; i++)
            {
                const auto* module = resp->modules[i];
                const size_t nameLen = sl::memfirst(module->cmdline, 0, 0);
                if (sl::memcmp(module->cmdline, "northport-initdisk", nameLen) != 0)
                    continue;
                
                Log("Found module \"%s\" (@ 0x%lx), loading as initdisk.", LogLevel::Info, 
                    module->cmdline, (uintptr_t)module->address);
                
                //create a mountpoint for the initdisk under `/initdisk/`
                NodeProps mountProps { .name = "initdisk" };
                auto mountpoint = RootFilesystem()->GetRoot()->Create(NodeType::Directory, mountProps);
                ASSERT(mountpoint, "Failed to create initdisk mountpoint");

                //create filesystem, load module as initdisk
                TempFs* initdiskFs = new TempFs();
                initdiskFs->LoadInitdisk(module->address, module->size);

                //mount it!
                filesystems.EmplaceBack(initdiskFs);
                MountArgs mountArgs {};
                const bool mounted = initdiskFs->Mount(*mountpoint, mountArgs);
                if (!mounted)
                {
                    Log("Mounting initdisk failed, freeing resources.", LogLevel::Debug);
                    initdiskFs->Unmount(); //try to unmount, since we dont know how far the mounting process got
                    filesystems.PopBack();
                    delete initdiskFs;
                    delete *mountpoint; //TOOD: decrement ref count, not delete!
                }
                
                //only load the first module with this name for now, since we dont support
                //multiple init ramdisks.
                break;
            }
        }

        filesystemsLock.WriterUnlock();
    }

    Vfs* RootFilesystem()
    {
        return filesystems.Size() > 0 ? filesystems[0] : nullptr;
    }
}
