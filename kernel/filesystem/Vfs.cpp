#include <filesystem/Vfs.h>
#include <debug/Log.h>
#include <containers/Vector.h>
#include <filesystem/TempFs.h>

namespace Npk::Filesystem
{
    sl::Vector<Vfs*> filesystems;
    
    void InitVfs()
    {
        //TODO: detect and mount the root filesystem based on config data.
        //For now we just mount a tmpfs as root
        filesystems.EmplaceBack(new TempFs());
        Log("VFS initialized, tempfs mounted as root.", LogLevel::Info);

        auto rfs = RootFilesystem();
        NodeDetails deets { .name = "parent" };
        auto maybe0 = rfs->Create(rfs->GetRoot(), NodeType::Directory, deets);
        
        deets.name = "file0.txt";
        auto maybe1 = rfs->Create(*maybe0, NodeType::File, deets);

        deets.name = "somedir";
        auto maybe2 = rfs->Create(*maybe0, NodeType::Directory, deets);
        rfs->Create(*maybe1, NodeType::File, deets); //should fail: parent not a dir
        rfs->Create(*maybe2, NodeType::File, deets); //this should be fine.

        auto maybeAlso2 = rfs->GetNode("parent/file0.txt");
        Log("Found parent/file0.txt: %s", LogLevel::Debug, maybeAlso2 ? "yes" : "no");

        Log("Done!", LogLevel::Debug);
        Halt();
    }

    Vfs* RootFilesystem()
    {
        return filesystems.Size() > 0 ? filesystems[0] : nullptr;
    }
}
