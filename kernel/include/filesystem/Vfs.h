#pragma once

#include <Atomic.h>
#include <Time.h>
#include <String.h>
#include <Handle.h>
#include <Locks.h>

namespace Npk::Filesystem
{
    enum class NodeType
    {
        Unknown,
        File,
        Directory,
    };

    //Bitfield representing the permissions of a node (and access to child nodes).
    //Follows standard unix permissions.
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

    //Structure used for read and write commands. The control flags determine what type of operation
    //is performed (a read by default). The buffer should be populated by the source of the packet.
    struct RwPacket
    {
        bool write = false;
        bool truncate = false;

        size_t offset = 0;
        size_t length = 0;
        void* buffer = nullptr;
    };

    //Properties of a node that may require indirect access. These properties are managed
    //via the GetProps/SetProps functions of a node. The node itself only contains the bare
    //minimum required to function, and the rest is loaded (and cached) on demand.
    struct NodeProps
    {
        sl::String name;
        sl::TimePoint created {};
        sl::TimePoint lastRead {};
        sl::TimePoint lastWrite {};
        size_t size {};
        NodePerms perms {};
    };

    //Arguments passed to `Mount()`
    struct MountArgs
    {
        
    };

    //Represents a program's (or user's) access rights, current directory (for relative paths),
    //and any private caches of a file they might have.
    struct FsContext
    {
        
    };

    constexpr FsContext KernelFsCtxt = {};

    struct Node;
    struct FileCache;

    //A VFS driver is a single filesystem, mounted or unmounted. 
    class VfsDriver
    {
    protected:
        Node* mountedOn;

        VfsDriver() : mountedOn(nullptr)
        {}

    public:
        virtual ~VfsDriver() = default;

        //FS-level operations
        virtual void FlushAll() = 0;
        virtual Node* Root() = 0;
        virtual sl::Handle<Node> Resolve(sl::StringSpan path, const FsContext& context) = 0;
        virtual bool Mount(Node* mountpoint, const MountArgs& args) = 0;
        virtual bool Unmount() = 0;

        //node-level operations
        virtual sl::Handle<Node> Create(Node* dir, NodeType type, const NodeProps& props, const FsContext& context) = 0;
        virtual bool Remove(Node* dir, Node* target, const FsContext& context) = 0;
        virtual bool Open(Node* node, const FsContext& context) = 0;
        virtual bool Close(Node* node, const FsContext& context) = 0;
        virtual size_t ReadWrite(Node* node, const RwPacket& packet, const FsContext& context) = 0;
        virtual bool Flush(Node* node) = 0;
        virtual sl::Handle<Node> GetChild(Node* dir, size_t index, const FsContext& context) = 0;
        virtual bool GetProps(Node* node, NodeProps& props, const FsContext& context) = 0;
        virtual bool SetProps(Node* node, const NodeProps& props, const FsContext& context) = 0;
    };

    //The building block of the VFS: a node represents a single file-like object within
    //the graph. Only the bare minimum properties are stored here (lock, refcount and link ptrs),
    //to access the actual properties indirect access might be required (see GetProps/SetProps).
    struct Node
    {
    public:
        sl::RwLock lock;
        sl::Atomic<size_t> references;
        VfsDriver& driver;
        NodeType type;
        void* driverData;

        union 
        {
            FileCache* cache; //type::File = contains file cache info
            VfsDriver* mounted;//type::Dir = non-null if a vfs driver is mounted here.
        } link;

        Node(VfsDriver& owner, NodeType type)
        : references(1), driver(owner), type(type), driverData(nullptr), link{ .mounted = nullptr}
        {}

        Node(VfsDriver& owner, NodeType type, void* privateData)
        : references(1), driver(owner), type(type), driverData(privateData), link{ .mounted = nullptr}
        {}

        //these are just typed macros
        [[gnu::always_inline]]
        inline sl::Handle<Node> Create(NodeType type, const NodeProps& props, const FsContext& context)
        { return driver.Create(this, type, props, context); }

        [[gnu::always_inline]]
        inline bool Remove(Node* target, const FsContext& context)
        { return driver.Remove(this, target, context); }

        [[gnu::always_inline]]
        inline bool Open(const FsContext& context)
        { return driver.Open(this, context); }

        [[gnu::always_inline]]
        inline bool Close(const FsContext& context)
        { return driver.Close(this, context); }

        [[gnu::always_inline]]
        inline size_t ReadWrite(const RwPacket& packet, const FsContext& context)
        { return driver.ReadWrite(this, packet, context); }

        [[gnu::always_inline]]
        inline bool Flush()
        { return driver.Flush(this); }

        [[gnu::always_inline]]
        inline sl::Handle<Node> GetChild(size_t index, const FsContext& context)
        { return driver.GetChild(this, index, context); }

        [[gnu::always_inline]]
        inline bool GetProps(NodeProps& props, const FsContext& context)
        { return driver.GetProps(this, props, context); }

        [[gnu::always_inline]]
        inline bool SetProps(const NodeProps& props, const FsContext& context)
        { return driver.SetProps(this, props, context); }
    };
}
