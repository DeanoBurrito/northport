#pragma once

#include <Atomic.h>
#include <Span.h>
#include <Handle.h>
#include <Time.h>
#include <Locks.h>
#include <String.h>
#include <containers/LinkedList.h>
#include <Optional.h>

namespace Npk::Filesystem
{
    enum class NodeType
    {
        Unknown,
        File,
        Directory,
    };

    enum class NodePerms : size_t
    {
        None = 0,

        UserExec = 1 << 0,
        UserWrite = 1 << 1,
        UserRead = 1 << 2,

        GroupExec = UserExec << 4,
        GroupWrite = UserWrite << 4,
        GroupRead = UserRead << 4,
        
        OtherExec = GroupExec << 4,
        OtherWrite = GroupWrite << 4,
        OtherRead = GroupRead << 4,
    };

    struct RwPacket
    {
        bool write = false;
        bool truncate = false;

        size_t offset = 0;
        size_t length = 0;
        void* buffer = nullptr;
    };
    
    struct NodeProps
    {
        sl::String name;
        sl::TimePoint created {};
        sl::TimePoint lastRead {};
        sl::TimePoint lastWrite {};
        size_t size {};
        NodePerms perms {};
    };

    struct MountArgs
    {
        
    };

    struct FileCachePart
    {
        void* page;
        size_t offset;
        sl::Atomic<size_t> references;
    };

    struct VfsContext
    {
        //TODO: cwd, rights, private views
    };

    struct Node;
    
    class Vfs
    {
    protected:
        Node* mountedOn;

        Vfs() : mountedOn(nullptr)
        {}

    public:
        virtual ~Vfs() = default;

        [[gnu::always_inline]]
        inline Node* Mountpoint()
        { return mountedOn; }

        //vfs operations
        virtual void FlushAll() = 0;
        virtual Node* GetRoot() = 0;
        virtual sl::Handle<Node> GetNode(sl::StringSpan path) = 0;
        virtual bool Mount(Node* mountpoint, const MountArgs& args) = 0;
        virtual bool Unmount() = 0;

        //node operations
        virtual sl::Handle<Node> Create(Node* dir, NodeType type, const NodeProps& props) = 0;
        virtual bool Remove(Node* dir, Node* target) = 0;
        virtual bool Open(Node* node) = 0;
        virtual bool Close(Node* node) = 0;
        virtual size_t ReadWrite(Node* node, const RwPacket& packet) = 0;
        virtual bool Flush(Node* node) = 0;
        virtual sl::Handle<Node> GetChild(Node* dir, size_t index) = 0;
        virtual bool GetProps(Node* node, NodeProps& props) = 0;
        virtual bool SetProps(Node* node, const NodeProps& props) = 0;
    };

    struct Node 
    {
    public:
        sl::RwLock lock;
        sl::Atomic<size_t> references;
        sl::LinkedList<FileCachePart> parts;
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
        : references(1), owner(owner), type(type), fsData(nullptr),
        link { .mounted = nullptr }
        {}

        Node(Vfs* mount, Vfs& owner, NodeType type)
        : references(1), owner(owner), type(type), fsData(nullptr), 
        link { .mounted = mount }
        {}

        Node(Vfs& owner, NodeType type, void* fsData)
        : references(1), owner(owner), type(type), fsData(fsData),
        link { .mounted = nullptr }
        {}

        //below here are just typed macros
        [[gnu::always_inline]]
        inline sl::Handle<Node> Create(NodeType type, const NodeProps& props)
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
        inline size_t ReadWrite(const RwPacket& buff)
        { return owner.ReadWrite(this, buff); }

        [[gnu::always_inline]]
        inline bool Flush()
        { return owner.Flush(this); }

        [[gnu::always_inline]]
        inline sl::Handle<Node> GetChild(size_t index)
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
    sl::Handle<Node> VfsLookup(const VfsContext& context, sl::StringSpan path);
}
