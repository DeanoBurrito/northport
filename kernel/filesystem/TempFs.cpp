#include <filesystem/TempFs.h>
#include <containers/Vector.h>
#include <debug/Log.h>
#include <Memory.h>
#include <formats/Url.h>
#include <memory/Pmm.h>

namespace Npk::Filesystem
{
    //internal per-node data stored by the tempfs
    struct TempFsData
    {
        NodeProps props;
        Node* parent;
        //TODO: this may waste a lot of memory with lots of children, switch to a list?
        sl::Vector<Node*> children; //unused if node is a file.
    };

    sl::Handle<Node> TempFs::TraverseUp(Node* node) const
    {
        ASSERT_UNREACHABLE()
    }

    TempFs::TempFs()
    {
        TempFsData* rootData = new TempFsData;
        rootData->parent = nullptr;
        rootData->props.size = 0;
        root = new Node(*this, NodeType::Directory, rootData);
    }

    void TempFs::FlushAll()
    {} //no-op

    Node* TempFs::GetRoot()
    { return root; }

    sl::Handle<Node> TempFs::GetNode(sl::StringSpan path)
    {
        VALIDATE(path.Begin()[0] != '/', {}, "Absolute path passed to VFS driver");
        VALIDATE(!path.Empty(), {}, "Empty path passed to VFS driver");

        while (*(path.End() - 1) == 0 || *(path.End() - 1) == '/') //cleanup end of path
            path = path.Subspan(0, path.Size() - 1);
        
        const sl::Url parsedPath = sl::Url::Parse(path);
        sl::Handle<Node> scan = root;
        
        scan->lock.ReaderLock();
        for (auto pathSeg = parsedPath.GetNextSeg(); !pathSeg.Empty(); pathSeg = parsedPath.GetNextSeg(pathSeg))
        {
            //TODO: using memXYZ functions, should be using string-sensitive version
            if (scan->type != NodeType::Directory)
            {
                scan->lock.ReaderUnlock();
                Log("Bad path segment: not a directory.", LogLevel::Error);
                return {};
            }

            //check for special case paths: "." and ".."
            if (pathSeg.Size() == 1 && *pathSeg.Begin() == '.')
                continue; //'.' = do nothing and just consume token
            if (pathSeg.Size() == 2 && sl::memcmp(path.Begin(), "..", 2) == 0)
            {
                sl::Handle<Node> up = TraverseUp(*scan);
                if (!up.Valid())
                {
                    scan->lock.ReaderUnlock();
                    return nullptr;
                }
                
                scan = up;
                continue;
            }

            //if the node is a mountpoint, use the root of the mounted vfs
            if (scan->link.mounted != nullptr)
            {
                sl::Handle<Node> next = scan->link.mounted->GetRoot();
                next->lock.ReaderLock();
                scan->lock.ReaderUnlock();
                scan = next;
            }

            //now we can load the node's data, and check for a child with the matching name
            bool foundNext = false;
            const TempFsData* data = static_cast<const TempFsData*>(scan->fsData);
            for (size_t i = 0; i < data->children.Size(); i++)
            {
                sl::Handle<Node> child { data->children[i] };
                child->lock.ReaderLock();
                auto childData = static_cast<const TempFsData*>(child->fsData);
                if (childData->props.name != pathSeg)
                {
                    child->lock.ReaderUnlock();
                    continue;
                }

                scan->lock.ReaderUnlock();
                scan = child;
                foundNext = true;
                break;
            }

            if (!foundNext)
            {
                scan->lock.ReaderUnlock();
                return {};
            }
            if (pathSeg.End() == path.End())
            {
                scan->lock.ReaderUnlock();
                return scan;
            }
        }

        ASSERT_UNREACHABLE();
    }

    bool TempFs::Mount(Node* mountpoint, const MountArgs& args)
    {
        VALIDATE(mountedOn == nullptr, false, "TempFS already mounted!");
        VALIDATE(root != nullptr, false, "TempFS has no root!");
        VALIDATE(mountpoint != nullptr, false, "Mountpoint is null.");
        VALIDATE(mountpoint->type == NodeType::Directory, false, "Mountpoint must be a directory");
        
        (void)args;
        mountpoint->lock.WriterLock();

        //before attaching the vfs root here, check the directory is empty.
        auto mountpointData = static_cast<const TempFsData*>(mountpoint->fsData);
        if (mountpointData->children.Size() > 0)
        {
            Log("Mountpoint %s is not empty.", LogLevel::Error, mountpointData->props.name.C_Str());
            mountpoint->lock.WriterUnlock();
            return false;
        }

        mountpoint->link.mounted = this;
        Log("Mounting new TempFs at %s", LogLevel::Info, mountpointData->props.name.C_Str());
        mountpoint->lock.WriterUnlock();

        mountedOn = mountpoint;
        return true;
    }

    bool TempFs::Unmount()
    {
        ASSERT_UNREACHABLE();
    }
    
    sl::Handle<Node> TempFs::Create(Node* dir, NodeType type, const NodeProps& props)
    {
        VALIDATE(dir != nullptr, {}, "Parent is null");
        VALIDATE(dir->type == NodeType::Directory, {}, "Parent not a directory");

        dir->lock.WriterLock();
        TempFsData* parentData = static_cast<TempFsData*>(dir->fsData);

        //check that a child with the same name doesn't already exist
        for (size_t i = 0; i < parentData->children.Size(); i++)
        {
            auto* childData = static_cast<const TempFsData*>(parentData->children[i]->fsData);
            if (childData->props.name != props.name)
                continue;
            
            //name collision, abort operation.
            dir->lock.WriterUnlock();
            return {};
        }

        TempFsData* childData = new TempFsData;
        childData->parent = dir;
        childData->props = props;
        childData->props.size = 0;

        Node* child = new Node(*this, type, childData);
        parentData->children.PushBack(child);

        dir->lock.WriterUnlock();
        return child;
    }

    bool TempFs::Remove(Node* dir, Node* target)
    {
        VALIDATE(dir != nullptr, false, "Parent is null");
        VALIDATE(dir->type == NodeType::Directory, false, "Parent is not directory");
        VALIDATE(dir->fsData != nullptr, false, "Parent fsdata is null");
        VALIDATE(target != nullptr, false, "Target is null.");
        VALIDATE(target->fsData != nullptr, false, "Target fsdata is null.");
        VALIDATE(dir != target, false, "Parent and target are the same node.");

        dir->lock.WriterLock();
        TempFsData* parentData = static_cast<TempFsData*>(dir->fsData);
        for (size_t i = 0; i < parentData->children.Size(); i++)
        {
            if (parentData->children[i] != target)
                continue;

            parentData->children[i]->lock.WriterLock();
            TempFsData* childData = static_cast<TempFsData*>(target->fsData);
            childData->parent = nullptr;
            parentData->children.Erase(i);
            target->references--; //TODO: delete nodes with refcount == 0

            dir->lock.WriterUnlock();
            return true;
        }

        dir->lock.WriterUnlock();
        return false; //we exited the loop without finding the target
    }

    bool TempFs::Open(Node* node)
    { return true; (void)node; } //no-op

    bool TempFs::Close(Node* node)
    { return true; (void)node; } //no-op

    //TODO: replace with dedicated cache + constant time lookup (xarray/hashmap?)
    FileCachePart* GetCache(Node* node, size_t offset)
    {
        offset = sl::AlignDown(offset, PageSize);
        for (auto it = node->parts.Begin(); it != node->parts.End(); ++it)
        {
            if (offset == it->offset)
                return &*it;
        }

        //cache not present, add it
        FileCachePart* cache = &node->parts.EmplaceBack();
        cache->references = 1;
        cache->offset = offset;
        cache->page = (void*)PMM::Global().Alloc();

        return cache;
    }

    size_t TempFs::ReadWrite(Node* node, const RwPacket& packet)
    {
        VALIDATE(node != nullptr, 0, "Node is null");
        VALIDATE(node->type == NodeType::File, 0, "Node is not a file");
        VALIDATE(node->fsData != nullptr, 0, "Node fsdata is null");
        VALIDATE(packet.buffer != nullptr, 0, "Buffer is null");
        VALIDATE(packet.length > 0, 0, "Op length of zero.");

        //TODO: replace PageSize with value obtained from FileCache (so it doesnt have to be page aligned).
        size_t misalignment = packet.offset % PageSize;
        size_t opSize = 0;
        if (packet.write) //TODO: this function can probably be reduced
        {
            node->lock.WriterLock();
            TempFsData* data = static_cast<TempFsData*>(node->fsData);

            //handle a change in the file size (expansion or truncation if requested)
            data->props.size = sl::Max(packet.offset + packet.length, data->props.size);
            if (packet.truncate && packet.offset + packet.length < data->props.size)
            {
                data->props.size = packet.offset + packet.length;
                //TODO: drop caches beyond new end of file
            }

            const size_t opSizeLimit = sl::Min(packet.offset + packet.length, data->props.size);
            while (opSize < opSizeLimit)
            {
                const size_t copyLength = sl::Min(PageSize, opSizeLimit - opSize) - misalignment;
                const FileCachePart* part = GetCache(node, packet.offset + opSize);
                sl::memcopy(packet.buffer, opSize, AddHhdm(part->page), misalignment, copyLength);

                opSize += copyLength;
                misalignment = 0; //only the first entry can be misaligned
            }

            node->lock.WriterUnlock();
        }
        else
        {
            node->lock.ReaderLock();
            
            const TempFsData* data = static_cast<TempFsData*>(node->fsData);
            const size_t opSizeLimit = sl::Min(packet.offset + packet.length, data->props.size);

            while (opSize < opSizeLimit)
            {
                const size_t copyLength = sl::Min(PageSize, opSizeLimit - opSize) - misalignment;
                const FileCachePart* part = GetCache(node, packet.offset + opSize);
                sl::memcopy(AddHhdm(part->page), misalignment, packet.buffer, opSize, copyLength);

                opSize += copyLength;
                misalignment = 0; //only the first entry can be misaligned
            }

            node->lock.ReaderUnlock();
        }
        
        return opSize;
    }

    bool TempFs::Flush(Node* node)
    { return false; (void)node; } //no-op

    sl::Handle<Node> TempFs::GetChild(Node* dir, size_t index)
    {
        VALIDATE(dir != nullptr, {}, "Parent is null.");
        VALIDATE(dir->fsData != nullptr, {}, "Parent fsdata is null.");
        VALIDATE(dir->type == NodeType::Directory, {}, "Parent is not a directory");
        
        Node* found = nullptr;
        dir->lock.ReaderLock();
        const TempFsData* data = static_cast<const TempFsData*>(dir->fsData);
        if (index < data->children.Size())
            found = data->children[index];

        dir->lock.ReaderUnlock();
        return found;
    }

    bool TempFs::GetProps(Node* node, NodeProps& props)
    {
        VALIDATE(node != nullptr, false, "Node is null");
        VALIDATE(node->fsData != nullptr, false, "Node fsdata is null");
        
        node->lock.ReaderLock();
        const TempFsData* data = static_cast<const TempFsData*>(node->fsData);
        props = data->props;
        node->lock.ReaderUnlock();

        return true;
    }

    bool TempFs::SetProps(Node* node, const NodeProps& props)
    {
        VALIDATE(node != nullptr, false, "Node is null");
        VALIDATE(node->fsData != nullptr, false, "Node fsdata is null");
        
        node->lock.WriterLock();
        TempFsData* data = static_cast<TempFsData*>(node->fsData);
        //TODO: select which props to update: not size!
        data->props = props; //TODO: don't update the name if it hasn't changed, save some copying
        node->lock.WriterLock();

        return true;
    }
}
