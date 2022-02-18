#pragma once

#include <stdint.h>
#include <String.h>
#include <containers/Vector.h>
#include <filesystem/FilesystemDriver.h>

namespace Kernel::Filesystem
{
    enum class IoControl : uint64_t
    {
        
    };

    enum class VfsNodeType : uint64_t
    {
        Directory = 0,
        File = 1,
    };
    
    class VFS;
    
    struct VfsNode
    {
    friend VFS;
    friend FilesystemDriver;
    private:
        sl::Vector<VfsNode*> children;
        VfsNode* parent;
        sl::String name;
        FilesystemDriver* driver;
        VfsNodeType type;

        VfsNode(VfsNode* parent, const sl::String& name, FilesystemDriver* driver, VfsNodeType type) 
        : parent(parent), name(name), driver(driver), type(type)
        {}

        ~VfsNode();

    public:
        [[gnu::always_inline]] inline
        VfsNodeType GetType() const
        { return type; }

        size_t Read(size_t fromOffset, uint8_t* toBuffer, size_t toOffset, size_t readLength);
        size_t Write(size_t toOffset, uint8_t* fromBuffer, size_t fromOffset, size_t writeLength);
        void Flush();
        bool SetIoControl(IoControl control, uint64_t flags, void* args);
    };
}
