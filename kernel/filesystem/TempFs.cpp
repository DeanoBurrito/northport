#include <filesystem/TempFs.h>
#include <containers/Vector.h>
#include <debug/Log.h>
#include <Memory.h>
#include <formats/Tar.h>

namespace Npk::Filesystem
{
    //internal per-node data stored by the tempfs
    struct TempFsData
    {
        NodeProps props;
        sl::Vector<Node*> children; //unused if node is a file.
        uint8_t* buffer;
    };

    TempFs::TempFs()
    {
        TempFsData* rootData = new TempFsData;
        rootData->props.name = "/"; //TODO: this is a bit hacky - could we do better?
        root = new Node(*this, NodeType::Directory, rootData);
        root->references = 1;
    }

    void LoadTarEntry(Node* root, const sl::TarHeader* header)
    {
        VALIDATE(root != nullptr,, "TempFs root is nullptr");
        VALIDATE(header != nullptr,, "Tar for TempFs root is nullptr");

        ASSERT_UNREACHABLE()
    }

    void TempFs::LoadInitdisk(void* base, size_t length)
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
        while (*(path.End() - 1) == 0) //remove trailing NULL chars
            path = path.Subspan(0, path.Size() - 1);

        VALIDATE(path.Begin()[0] != '/', {}, "Absolute path passed to VFS driver");
        VALIDATE(path.End()[-1] != '/', {}, "Path cannot end with separator");

        size_t tokenBegin = 0;
        Node* scan = root;

        scan->lock.ReaderLock();
        while (true)
        {
            //TODO: we use mem* functions, what if text is not ASCII?
            const size_t tokenEnd = sl::Min(path.Size(), 
                sl::memfirst(&path[tokenBegin], '/', path.Size() - tokenBegin));
            const size_t tokenLength = tokenEnd - tokenBegin;

            if ((tokenEnd != path.Size() && scan->type != NodeType::Directory)
                || scan->fsData == nullptr)
            {
                scan->lock.ReaderUnlock();
                Log("Bad mid-patch node during vfs lookup.", LogLevel::Error);
                return {};
            }
            
            const TempFsData* data = static_cast<TempFsData*>(scan->fsData);
            for (size_t i = 0; i < data->children.Size(); i++)
            {
                data->children[i]->lock.ReaderLock();
                const TempFsData* childData = static_cast<TempFsData*>(data->children[i]->fsData);
                if (childData->props.name != path.Subspan(tokenBegin, tokenLength))
                {
                    data->children[i]->lock.ReaderUnlock();
                    continue;
                }

                scan->lock.ReaderUnlock();
                scan = data->children[i];
                break;
            }

            tokenBegin += tokenLength + 1;
            if (tokenBegin >= path.Size())
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

        TempFsData* parentData = static_cast<TempFsData*>(dir->fsData);
        dir->lock.WriterLock();

        //check that a child with the same name doesn't already exist
        for (size_t i = 0; i < parentData->children.Size(); i++)
        {
            const TempFsData* childData = static_cast<const TempFsData*>(parentData->children[i]->fsData);
            if (childData->props.name != props.name)
                continue;
            
            //name collision, abort operation.
            dir->lock.WriterUnlock();
            return {};
        }

        TempFsData* childData = new TempFsData;
        childData->props = props;
        childData->props.size = 0;

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
    { return true; (void)node; } //no-op

    bool TempFs::Close(Node* node)
    { return true; (void)node; } //no-op

    size_t TempFs::ReadWrite(Node* node, const RwBuffer& buff)
    {
        VALIDATE(node != nullptr, 0, "Node is null");
        VALIDATE(node->type == NodeType::File, 0, "Node is not a file");
        VALIDATE(node->fsData != nullptr, 0, "Node fsdata is null");
        VALIDATE(buff.buffer != nullptr, 0, "Buffer is null");

        size_t opSize = 0;
        if (buff.write)
        {
            node->lock.WriterLock();
            TempFsData* data = static_cast<TempFsData*>(node->fsData);

            //calculate new buffer size, this takes care of truncation and expansion for us.
            size_t newBufferSize = buff.offset + buff.length;
            if (!buff.truncate && data->props.size > newBufferSize)
                newBufferSize = data->props.size;
            
            //if buffer size is changing, allocate a new one and migrate data
            if (newBufferSize != data->props.size)
            {
                uint8_t* newBuffer = new uint8_t[newBufferSize];
                //TODO: we could be smarter and only copy things we're not about to overwrite
                //TODO: if we're expanding the file we probably want to zero dead space
                sl::memcopy(data->buffer, newBuffer, sl::Min(data->props.size, newBufferSize));

                if (data->props.size > 0)
                    delete[] data->buffer; //delete buffer if we've previously allocated it
                data->props.size = newBufferSize;
                data->buffer = newBuffer;
            }

            //now actually write the data to the tempfs
            sl::memcopy(buff.buffer, 0, data->buffer, buff.offset, buff.length);
            node->lock.WriterUnlock();
            opSize = buff.length;
        }
        else
        {
            node->lock.ReaderLock();
            const TempFsData* data = static_cast<const TempFsData*>(node->fsData);

            //ensure there's data to read, and then perform a bounds check on the buffer
            if (data->props.size > 0)
            {
                opSize = sl::Min(buff.length, data->props.size - buff.offset);
                sl::memcopy(data->buffer, buff.offset, buff.buffer, 0, opSize);
            }
            node->lock.ReaderUnlock();
        }
        
        return opSize;
    }

    bool TempFs::Flush(Node* node)
    { return false; (void)node; } //no-op

    sl::Opt<Node*> TempFs::GetChild(Node* dir, size_t index)
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
        return found == nullptr ? sl::Opt<Node*>{} : found;
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
        data->props = props; //TODO: don't update the name if it hasn't changed, save some copying
        node->lock.WriterLock();

        return true;
    }
}
