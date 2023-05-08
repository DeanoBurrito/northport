#pragma once

#include <filesystem/Vfs.h>

namespace Npk::Filesystem
{
    class TempFs : public Vfs
    {
    private:
        Node* root;

        sl::Handle<Node> TraverseUp(Node* node) const;

    public:
        TempFs();

        void FlushAll() override;
        Node* GetRoot() override;
        sl::Handle<Node> GetNode(sl::StringSpan path) override;
        bool Mount(Node* mountpoint, const MountArgs& args) override;
        bool Unmount() override;

        sl::Handle<Node> Create(Node* dir, NodeType type, const NodeProps& props) override;
        bool Remove(Node* dir, Node* target) override;
        bool Open(Node* node) override;
        bool Close(Node* node) override;
        size_t ReadWrite(Node* node, const RwPacket& packet) override;
        bool Flush(Node* node) override;
        sl::Handle<Node> GetChild(Node* dir, size_t index) override;
        bool GetProps(Node* node, NodeProps& props) override;
        bool SetProps(Node* node, const NodeProps& props) override;
    };
}
