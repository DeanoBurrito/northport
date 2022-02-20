#pragma once

#include <drivers/GenericDriver.h>
#include <String.h>

namespace Kernel::Filesystem
{
    struct VfsNode;
    enum class VfsNodeType : uint64_t;
    class VFS;

    enum class NodeChangedFlags
    {
        None = 0,
        Added = (1 << 0),
        Removed = (1 << 1),
    };
    
    struct NodeChangedEventArgs
    {
        NodeChangedFlags flags;
        Filesystem::VfsNode* node;
        bool cancel;

        NodeChangedEventArgs(NodeChangedFlags flags, VfsNode* node)
        : flags(flags), node(node), cancel(false)
        {}
    };
    
    class FilesystemDriver : public Drivers::GenericDriver
    {
    friend VFS;
    friend VfsNode;
    protected:
        VfsNode* CreateNode(VfsNode* parent, const sl::String& name, VfsNodeType type);
        void AddChildToNode(VfsNode* parent, VfsNode* child);
        void*& NodeData(VfsNode* node);
        
        virtual void PopulateNode(VfsNode* root) = 0;
        virtual bool PrepareForUnmount(bool force = false) = 0;

        virtual size_t DoRead(VfsNode* node, size_t fromOffset, uint8_t* toBuffer, size_t toOffset, size_t readLength) = 0;
        virtual size_t DoWrite(VfsNode* node, size_t toOffset, uint8_t* fromBuffer, size_t fromOffset, size_t writeLength) = 0;
        virtual void DoFlush(VfsNode* node) = 0;

    public:
        virtual void Init(Drivers::DriverInitInfo* initInfo) = 0;
        virtual void Deinit() = 0;
        virtual void HandleEvent(Drivers::DriverEventType type, void* eventArg) = 0;
    };
}
