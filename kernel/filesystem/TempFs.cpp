#include <filesystem/TempFs.h>
#include <debug/Log.h>
#include <containers/Vector.h>
#include <Memory.h>

namespace Npk::Filesystem
{
    struct TempFsData
    {
        NodeDetails details;
        sl::Vector<Node*> children; //unused if node is a file.
    };

    TempFs::TempFs()
    {
        TempFsData* rootData = new TempFsData;
        rootData->details.name = "/"; //TODO: this is a bit hacky - could we do better?
        root = new Node(*this, NodeType::Directory, rootData);
        root->references = 1;
    }
    
    void TempFs::Flush()
    {} //no-op

    Node* TempFs::GetRoot()
    { return root; }

    sl::Opt<Node*> TempFs::GetNode(sl::StringSpan path)
    {
        size_t tokenBegin = 0;
        Node* scan = root;

        while (true)
        {
            VALIDATE(scan != nullptr, {}, "Unexpected end of vfs path.");

            size_t tokenEnd = sl::memfirst(path.Begin(), '/', path.Size() - tokenBegin);
            if (tokenEnd == -1ul)
            {
                //we're at the end of the path, check if this is the node
                tokenEnd = path.Size();
                
                const TempFsData* data = static_cast<TempFsData*>(scan->fsData);
                if (sl::memcmp(data->details.name, &path[tokenBegin], tokenEnd - tokenBegin) == 0)
                    return scan; //name matched, this is the node
                return {};
            }

            VALIDATE(scan->type == NodeType::Directory, {}, "Mid-path node is not directory");
            VALIDATE(scan->fsData != nullptr, {}, "No node data");
            
            const TempFsData* data = static_cast<TempFsData*>(scan->fsData);
            for (size_t i = 0; i < data->children.Size(); i++)
            {
                const TempFsData* childData = static_cast<TempFsData*>(data->children[i]->fsData);
                if (sl::memcmp(childData->details.name, &path[tokenBegin], tokenEnd - tokenBegin) != 0)
                    continue;
                
                scan = data->children[i];
                break;
            }

            tokenBegin = tokenEnd;
        }
        
        ASSERT_UNREACHABLE();
    }
    
    sl::Opt<Node*> TempFs::Create(Node* dir, NodeType type, const NodeDetails& details)
    {
        VALIDATE(dir->type == NodeType::Directory, {}, "Parent not a directory");
        VALIDATE(details.name != nullptr && details.name[0] != 0, {}, "Bad name");

        TempFsData* childData = new TempFsData;
        childData->details = details; //copy
        Node* child = new Node(*this, type, childData);

        dir->lock.WriterLock();
        TempFsData* parentData = static_cast<TempFsData*>(dir->fsData);
        parentData->children.PushBack(child);
        dir->lock.WriterUnlock();
        
        Log("VfsNode '%s' has new child '%s', type=%u", LogLevel::Debug, parentData->details.name, details.name, (unsigned)type);
        return child;
    }

    bool TempFs::Open(Node* node)
    {
        ASSERT_UNREACHABLE()
    }

    bool TempFs::Close(Node* node)
    {
        ASSERT_UNREACHABLE()
    }
}
