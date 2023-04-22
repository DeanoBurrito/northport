#include <filesystem/Vfs.h>
#include <debug/Log.h>
#include <containers/Vector.h>
#include <filesystem/TempFs.h>

namespace Npk::Filesystem
{
    sl::Vector<Vfs*> filesystems;

    void PrintNode(size_t depth, Node* node)
    {
        constexpr const char* TypeStrs[] = { "Unknown", "File", "Dir" };
        
        char indent[depth + 1];
        sl::memset(indent, ' ', depth);
        indent[depth] = 0;

        NodeProps props {};
        ASSERT(node->owner.GetProps(node, props), "GetProps() failed");

        Log("%s%s: %s%s", LogLevel::Debug, indent, TypeStrs[(size_t)node->type], 
            props.name, node->type == NodeType::Directory ? "/" : "");
        if (node->type != NodeType::Directory)
            return;

        for (size_t i = 0; true; i++)
        {
            auto child = node->owner.GetChild(node, i);
            if (!child)
                return;
            PrintNode(depth + 2, *child);
        }
    }
    
    void InitVfs()
    {
        //TODO: detect and mount the root filesystem based on config data.
        //For now we just mount a tmpfs as root.
        //TODO: how do we want to handle no root? - empty tempfs?
        filesystems.EmplaceBack(new TempFs());
        Log("VFS initialized, tempfs mounted as root.", LogLevel::Info);

        auto rfs = RootFilesystem();
        NodeProps deets { .name = "parent" };
        auto maybe0 = rfs->Create(rfs->GetRoot(), NodeType::Directory, deets);
        
        deets.name = "file0.txt";
        auto maybe1 = rfs->Create(*maybe0, NodeType::File, deets);

        deets.name = "somedir";
        auto maybe2 = rfs->Create(*maybe0, NodeType::Directory, deets);
        rfs->Create(*maybe1, NodeType::File, deets); //should fail: parent not a dir
        rfs->Create(*maybe2, NodeType::File, deets); //this should be fine.

        PrintNode(0, rfs->GetRoot());

        Log("Done!", LogLevel::Debug);
        Halt();
    }

    Vfs* RootFilesystem()
    {
        return filesystems.Size() > 0 ? filesystems[0] : nullptr;
    }
}
