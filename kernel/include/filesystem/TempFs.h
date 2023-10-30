#pragma once

#include <filesystem/Vfs.h>

namespace Npk::Filesystem
{
    class TempFs : public VfsDriver
    {
    private:
        Node* root;

        sl::Handle<Node> TraverseUp(Node* node) const;

    public:
        TempFs();

        void FlushAll() override;
        Node* Root() override;
        sl::Handle<Node> Resolve(sl::StringSpan path, const FsContext& context) override;
        bool Mount(Node* mountpoint, const MountArgs& args) override;
        bool Unmount() override;

        sl::Handle<Node> Create(Node* dir, NodeType type, const NodeProps& props, const FsContext& context) override;
        bool Remove(Node* dir, Node* target, const FsContext& context) override;
        bool Open(Node* node, const FsContext& context) override;
        bool Close(Node* node, const FsContext& context) override;
        size_t ReadWrite(Node* node, const RwPacket& packet, const FsContext& context) override;
        bool Flush(Node* node) override;
        sl::Handle<Node> GetChild(Node* dir, size_t index, const FsContext& context) override;
        sl::Handle<Node> FindChild(Node* dir, sl::StringSpan name, const FsContext& context) override;
        bool GetProps(Node* node, NodeProps& props, const FsContext& context) override;
        bool SetProps(Node* node, const NodeProps& props, const FsContext& context) override;
    };
}
