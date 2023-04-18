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

        void Flush() override;
        Node* GetRoot() override;
        sl::Opt<Node*> GetNode(sl::StringSpan path) override;

        sl::Opt<Node*> Create(Node* dir, NodeType type, const NodeDetails& details) override;
        bool Open(Node* node) override;
        bool Close(Node* node) override;
    };
}
