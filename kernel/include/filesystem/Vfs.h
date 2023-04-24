#pragma once

#include <Atomic.h>
#include <Span.h>
#include <Optional.h>
#include <Time.h>
#include <Locks.h>
#include <String.h>

namespace Npk::Filesystem
{
    enum class NodeType
    {
        Unknown,
        File,
        Directory,
    };

    struct RwBuffer
    {
        bool write;
        bool truncate;

        size_t offset;
        size_t length;
        void* buffer;
    };
    
    struct NodeProps
    {
        sl::String name;
        sl::TimePoint created {};
        sl::TimePoint lastRead {};
        sl::TimePoint lastWrite {};
        size_t size;
        //TODO: permissions, do we want to go unix style?
    };

    struct Node;
    
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

        virtual void FlushAll() = 0;
        virtual Node* GetRoot() = 0;
        virtual sl::Opt<Node*> GetNode(sl::StringSpan path) = 0;

        virtual sl::Opt<Node*> Create(Node* dir, NodeType type, const NodeProps& props) = 0;
        virtual bool Remove(Node* dir, Node* target) = 0;
        virtual bool Open(Node* node) = 0;
        virtual bool Close(Node* node) = 0;
        virtual size_t ReadWrite(Node* node, const RwBuffer& buff) = 0;
        virtual bool Flush(Node* node) = 0;
        virtual sl::Opt<Node*> GetChild(Node* dir, size_t index) = 0;
        virtual bool GetProps(Node* node, NodeProps& props) = 0;
        virtual bool SetProps(Node* node, const NodeProps& props) = 0;
    };

    struct Node 
    {
    public:
        sl::RwLock lock;
        sl::Atomic<size_t> references; //TODO: RAII this (maybe we have sl::Handle<T> ?)
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

        //below here are just typed macros
        [[gnu::always_inline]]
        inline sl::Opt<Node*> Create(NodeType type, const NodeProps& props)
        { return owner.Create(this, type, props); }

        [[gnu::always_inline]]
        inline bool Remove(Node* target)
        { return owner.Remove(this, target); }
        
        [[gnu::always_inline]]
        inline bool Open()
        { return owner.Open(this);}

        [[gnu::always_inline]]
        inline bool Close()
        { return owner.Close(this); }

        [[gnu::always_inline]]
        inline size_t ReadWrite(const RwBuffer& buff)
        { return owner.ReadWrite(this, buff); }

        [[gnu::always_inline]]
        inline bool Flush()
        { return owner.Flush(this); }

        [[gnu::always_inline]]
        inline sl::Opt<Node*> GetChild(size_t index)
        { return owner.GetChild(this, index); }

        [[gnu::always_inline]]
        inline bool GetProps(NodeProps& props)
        { return owner.GetProps(this, props); }

        [[gnu::always_inline]]
        inline bool SetProps(const NodeProps& props)
        { return owner.SetProps(this, props); }
    };

    void InitVfs();
    Vfs* RootFilesystem();
}
