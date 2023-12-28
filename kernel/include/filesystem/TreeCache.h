#pragma once

#include <filesystem/Filesystem.h>
#include <filesystem/FileCache.h>

namespace Npk::Filesystem
{
    struct VfsNode
    {
        sl::Atomic<size_t> references;
        sl::RwLock metadataLock;

        NodeType type;
        sl::Handle<FileCache> cache;
        void* driverData;

        VfsId id;
        VfsId bond;
    };

    void InitTreeCache();

    sl::Handle<VfsNode, sl::NoHandleDtor> GetVfsNode(VfsId id, bool traverseLinks);
}
