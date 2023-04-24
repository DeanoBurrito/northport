#pragma once

#include <filesystem/Vfs.h>

namespace Npk::Filesystem
{
    class TempFs : public Vfs
    {
    private:
        Node* root;

    public:
        TempFs();

        void LoadInitdisk(void* base, size_t length);

        void FlushAll() override;
        Node* GetRoot() override;
        sl::Opt<Node*> GetNode(sl::StringSpan path) override;

        sl::Opt<Node*> Create(Node* dir, NodeType type, const NodeProps& props) override;
        bool Remove(Node* dir, Node* target) override;
        bool Open(Node* node) override;
        bool Close(Node* node) override;
        size_t ReadWrite(Node* node, const RwBuffer& buff) override;
        bool Flush(Node* node) override;
        sl::Opt<Node*> GetChild(Node* dir, size_t index) override;
        bool GetProps(Node* node, NodeProps& props) override;
        bool SetProps(Node* node, const NodeProps& props) override;
    };
}
