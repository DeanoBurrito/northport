#pragma once

#include <Span.h>
#include <Optional.h>
#include <Atomic.h>
#include <Time.h>
#include <Locks.h>

namespace Npk::Filesystem
{
    class Vfs;

    enum class NodeType
    {
        Unknown,
        File,
        Directory,
    };
    
    struct NodeDetails
    {
        const char* name = nullptr;
        sl::TimePoint created {};
        sl::TimePoint lastRead {};
        sl::TimePoint lastWrite {};
        //TODO: permissions, do we want to go unix style?
    };
    
    class Node
    {
    public:
        sl::RwLock lock;
        sl::Atomic<size_t> references;
        Vfs& owner;
        NodeType type;
        void* fsData;

        union 
        {
            Vfs* mounted;
            //NOTE: this seems silly right now, but it will also contain other
            //linkage info in the future (like unix sockets).
        } link;

        Node(Vfs& owner, NodeType type)
        : references(0), owner(owner), type(type), fsData(nullptr)
        {}

        Node(Vfs* mount, Vfs& owner, NodeType type)
        : references(0), owner(owner), type(type), fsData(nullptr), 
        link { .mounted = mount }
        {}

        Node(Vfs& owner, NodeType type, void* fsData)
        : references(0), owner(owner), type(type), fsData(fsData)
        {}
    };
    
    class Vfs
    {
    private:
        Node* mountedOn;
    
    protected:
        Vfs() : mountedOn(nullptr)
        {}

    public:
        [[gnu::always_inline]]
        inline Node* Mountpoint()
        { return mountedOn; }

        virtual void Flush() = 0;
        virtual Node* GetRoot() = 0;
        virtual sl::Opt<Node*> GetNode(sl::StringSpan path) = 0;

        virtual sl::Opt<Node*> Create(Node* dir, NodeType type, const NodeDetails& details) = 0;
        virtual bool Open(Node* node) = 0;
        virtual bool Close(Node* node) = 0;
    };

    void InitVfs();
    Vfs* RootFilesystem();
}
