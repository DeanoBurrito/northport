#pragma once

#include <stdint.h>
#include <String.h>
#include <containers/Vector.h>
#include <filesystem/FilesystemDriver.h>
#include <filesystem/VfsNode.h>

namespace Kernel::Filesystem
{    
    struct VfsMountPoint
    {
        VfsNode* mountedAt;
        const sl::String& prefix;
        FilesystemDriver* driver;

        VfsMountPoint(VfsNode* mountedAt, const sl::String& prefix, FilesystemDriver* driver) : mountedAt(mountedAt), prefix(prefix), driver(driver)
        {}
    };

    class VFS
    {
    private:
        sl::Vector<VfsMountPoint*>* mountPoints;
        VfsNode* rootNode;

        char mountLock;

        VfsNode* FindNode(const VfsNode* start, const sl::String& path, size_t pathTrimStart);

    public:
        static VFS* Global();
        void Init();

        bool Mount(FilesystemDriver* driverInstance, const sl::String& prefix);
        void Unmount(const sl::String& prefix, bool force);
    };
}
