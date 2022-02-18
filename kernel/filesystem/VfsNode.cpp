#include <filesystem/VfsNode.h>

namespace Kernel::Filesystem
{
    VfsNode::~VfsNode()
    {
        //we're only responsible for any child nodes, not the parent or driver
        for (size_t i = 0; i < children.Size(); i++)
            delete children.At(i);
        children.Clear();
    }
    
    size_t VfsNode::Read(size_t fromOffset, uint8_t* toBuffer, size_t toOffset, size_t readLength)
    {
        //TODO: memory-caching of files. This is where we would handle that, on the vfs node, before it's passed off to the driver layer
        if (driver)
            return driver->DoRead(this, fromOffset, toBuffer, toOffset, readLength);
        return 0;
    }

    size_t VfsNode::Write(size_t toOffset, uint8_t* fromBuffer, size_t fromOffset, size_t writeLength)
    {
        if (driver)
            return driver->DoWrite(this, toOffset, fromBuffer, fromOffset, writeLength);
        return 0;
    }

    void VfsNode::Flush()
    {
        if (driver)
            driver->DoFlush(this);
    }

    bool VfsNode::SetIoControl(IoControl control, uint64_t flags, void* args)
    { 
        (void)control; (void)flags; (void)args;
        return false;
    }
}
