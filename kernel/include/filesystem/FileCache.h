#pragma once

#include <stddef.h>
#include <Atomic.h>
#include <Handle.h>
#include <containers/LinkedList.h>
#include <stdint.h>

namespace Npk::Filesystem
{
    struct FileCacheUnit
    {
        uintptr_t physBase;
        size_t offset;
        sl::Atomic<size_t> references;
        //TODO: track dirty state
    };

    struct FileCache
    {
        sl::LinkedList<FileCacheUnit> units; //TODO: rbtree?
    };

    struct FileCacheInfo
    {
        size_t unitSize;
        size_t hatMode;
        size_t modeMultiple;
    };

    //Does what you'd expect, initializes some global state for the file cache.
    void InitFileCache();

    //Returns info about the size and configuration of the memory the file cache
    //is using.
    FileCacheInfo GetFileCacheInfo();

    sl::Handle<FileCacheUnit> GetFileCache(FileCache* cache, size_t offset, bool createNew);
}
