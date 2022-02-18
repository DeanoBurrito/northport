#include <filesystem/FilesystemDriver.h>
#include <filesystem/VfsNode.h>

namespace Kernel::Filesystem
{
    VfsNode* FilesystemDriver::CreateNode(VfsNode* parent, const sl::String& name, VfsNodeType type)
    { 
        VfsNode* node = new VfsNode(parent, name, this, type); 
        if (parent != nullptr)
            parent->children.PushBack(node);
        return node;
    }

    void FilesystemDriver::AddChildToNode(VfsNode* parent, VfsNode* child)
    {
        if (parent != nullptr && child != nullptr)
        {
            parent->children.PushBack(child);
            child->parent = parent;
        }
    }
}
