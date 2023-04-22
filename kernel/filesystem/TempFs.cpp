#include <filesystem/TempFs.h>
#include <containers/Vector.h>
#include <debug/Log.h>
#include <Memory.h>
#include <formats/Tar.h>

namespace Npk::Filesystem
{
    struct TempFsData
    {
        NodeProps props;
        sl::Vector<Node*> children; //unused if node is a file.
    };

    TempFs::TempFs()
    {
        TempFsData* rootData = new TempFsData;
        rootData->props.name = "/"; //TODO: this is a bit hacky - could we do better?
        root = new Node(*this, NodeType::Directory, rootData);
        root->references = 1;
    }

    void TempFs::PopulateFromInitdisk(void* base, size_t length)
    {
        VALIDATE(base != nullptr && length > sl::TarBlockSize,, "Initdisk has weird starting values.");
        ASSERT_UNREACHABLE()
    }
    
    void TempFs::FlushAll()
    {} //no-op

    Node* TempFs::GetRoot()
    { return root; }

    sl::Opt<Node*> TempFs::GetNode(sl::StringSpan path)
    {
        size_t tokenBegin = 0;
        Node* scan = root;

        scan->lock.ReaderLock();
        while (true)
        {
            VALIDATE(scan != nullptr, {}, "Unexpected end of vfs path.");
            VALIDATE(scan->type == NodeType::Directory, {}, "Mid-path node is not directory");
            VALIDATE(scan->fsData != nullptr, {}, "No node data");

            //TODO: we're using mem* functions here, but these strings might not be ASCII
            size_t tokenEnd = sl::memfirst(&path[tokenBegin], '/', path.Size() - tokenBegin);
            if (tokenEnd == -1ul)
                tokenEnd = path.Size();

            const TempFsData* data = static_cast<TempFsData*>(scan->fsData);
            for (size_t i = 0; i < data->children.Size(); i++)
            {
                data->children[i]->lock.ReaderLock();
                const TempFsData* childData = static_cast<TempFsData*>(data->children[i]->fsData);
                if (sl::memcmp(childData->props.name, &path[tokenBegin], tokenEnd - tokenBegin) != 0)
                {
                    data->children[i]->lock.ReaderUnlock();
                    continue;
                }
                
                scan->lock.ReaderUnlock();
                scan = data->children[i];
                break;
            }

            tokenBegin = tokenEnd + 1;
            if (tokenEnd == path.Size())
            {
                scan->lock.ReaderUnlock();
                if (data == scan->fsData) //true if we didnt update scan, meaning no pathname match
                    return {};
                return scan;
            }
        }
        
        ASSERT_UNREACHABLE();
    }
    
    sl::Opt<Node*> TempFs::Create(Node* dir, NodeType type, const NodeProps& props)
    {
        VALIDATE(dir != nullptr, {}, "Parent is null");
        VALIDATE(dir->type == NodeType::Directory, {}, "Parent not a directory");
        VALIDATE(props.name != nullptr && props.name[0] != 0, {}, "Bad name");

        TempFsData* parentData = static_cast<TempFsData*>(dir->fsData);
        const size_t nameLen = sl::memfirst(props.name, 0, 0);

        //before adding the child, ensure the name isn't already in use.
        dir->lock.WriterLock();
        for (size_t i = 0; i < parentData->children.Size(); i++)
        {
            auto* childData = static_cast<const TempFsData*>(parentData->children[i]->fsData);
            const size_t childNameLen = sl::memfirst(childData->props.name, 0, 0);

            if (childNameLen != nameLen)
                continue;
            if (sl::memcmp(childData->props.name, props.name, nameLen) != 0)
                continue;
            
            //name matches an existing child of this node, reject it.
            dir->lock.WriterUnlock();
            return {};
        }
        
        TempFsData* childData = new TempFsData;
        childData->props = props; //TODO: we'll want to make a copy of the name here, in case the original is destroyed
        Node* child = new Node(*this, type, childData);
        parentData->children.PushBack(child);

        dir->lock.WriterUnlock();
        return child;
    }

    bool TempFs::Remove(Node* dir, Node* target)
    {
        ASSERT_UNREACHABLE()
    }

    bool TempFs::Open(Node* node)
    {
        ASSERT_UNREACHABLE()
    }

    bool TempFs::Close(Node* node)
    {
        ASSERT_UNREACHABLE()
    }

    size_t TempFs::ReadWrite(Node* node, const RwBuffer& buff)
    {
        ASSERT_UNREACHABLE()
    }

    bool TempFs::Flush(Node* node)
    {
        ASSERT_UNREACHABLE()
    }

    sl::Opt<Node*> TempFs::GetChild(Node* dir, size_t index)
    {
        VALIDATE(dir != nullptr, {}, "Parent is null.");
        VALIDATE(dir->type == NodeType::Directory, {}, "Parent is not a directory");
        
        Node* found = nullptr;
        dir->lock.ReaderLock();
        const TempFsData* data = static_cast<const TempFsData*>(dir->fsData);
        if (index < data->children.Size())
            found = data->children[index];

        dir->lock.ReaderUnlock();
        return found == nullptr ? sl::Opt<Node*>{} : found;
    }

    sl::Opt<Node*> TempFs::FindChild(Node* dir, sl::StringSpan name)
    {
        ASSERT_UNREACHABLE()
    }

    bool TempFs::GetProps(Node* node, NodeProps& props)
    {
        VALIDATE(node != nullptr, false, "Node is null");
        
        node->lock.ReaderLock();
        const TempFsData* data = static_cast<const TempFsData*>(node->fsData);
        props = data->props;
        node->lock.ReaderUnlock();

        return true;
    }

    bool TempFs::SetProps(Node* node, const NodeProps& props)
    {
        VALIDATE(node != nullptr, false, "Node is null");
        
        node->lock.WriterLock();
        TempFsData* data = static_cast<TempFsData*>(node->fsData);
        data->props = props; //TODO: handle copying string properly
        node->lock.WriterLock();

        return true;
    }
}
