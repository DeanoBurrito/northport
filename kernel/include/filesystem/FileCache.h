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
    };

    struct FileCache
    {
        sl::LinkedList<FileCacheUnit> units; //TODO: some kind of tree
    };

    //Does what you'd expect, initializes some global state for the file cache.
    void InitFileCache();

    //Returns the granule size used by the file cache: the size of cached parts of a file.
    //Typically this is the page size used by the MMU, but it may be bigger.
    size_t FileCacheUnitSize();

    //Obtains part of a cached file, optionally creating a new cache if allowed (for writes).
    //Otherwise it returns an empty handle (for reads).
    sl::Handle<FileCacheUnit> GetFileCache(FileCache* cache, size_t offset, bool createNew);
}
