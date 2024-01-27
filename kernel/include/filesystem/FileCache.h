#pragma once

#include <filesystem/Filesystem.h>
#include <stddef.h>
#include <Atomic.h>
#include <Handle.h>
#include <containers/LinkedList.h>
#include <stdint.h>

namespace Npk::Filesystem
{
    struct FileCache;

    struct FileCacheUnit
    {
        sl::Atomic<size_t> references;
        FileCache* owner;
        uintptr_t physBase;
        size_t offset;
        //TODO: track dirty and writeback-enable states
    };

    void CleanupFileCacheUnit(FileCacheUnit* unit);

    using FileCacheUnitHandle = sl::Handle<FileCacheUnit, CleanupFileCacheUnit>;

    struct FileCache
    {
        sl::Atomic<size_t> references;
        VfsId id;

        sl::SpinLock lock;
        size_t length;
        sl::LinkedList<FileCacheUnitHandle> units; //TODO: more efficient datastructure (rbtree?)
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

    bool SetFileCacheLength(sl::Handle<FileCache> cache, size_t length);
    sl::Handle<FileCache> GetFileCache(VfsId id);
    FileCacheUnitHandle GetFileCacheUnit(sl::Handle<FileCache> cache, size_t fileOffset);
    void CreateFileCacheEntries(sl::Handle<FileCache> fileCache, uintptr_t paddr, size_t size);
}
