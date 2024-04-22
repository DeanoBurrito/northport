#pragma once

#include <filesystem/Filesystem.h>
#include <stddef.h>
#include <Atomic.h>
#include <Handle.h>
#include <containers/RBTree.h>
#include <stdint.h>

namespace Npk::Filesystem
{
    struct FileCache;

    enum class FcuFlag
    {
        Writable,
        Dirty,
    };

    using FcuFlags = sl::Flags<FcuFlag>;

    struct FileCacheUnit
    {
        sl::RBTreeHook hook;
        sl::Atomic<size_t> references;

        FileCache* owner;
        uintptr_t physBase;
        size_t offset;
        FcuFlags flags;
    };

    struct FcuLess
    {
        bool operator()(const FileCacheUnit& a, const FileCacheUnit& b)
        { return a.physBase < b.physBase; }
    };

    void CleanupFileCacheUnit(FileCacheUnit* unit);

    using FileCacheUnitHandle = sl::Handle<FileCacheUnit, CleanupFileCacheUnit>;
    using FcuTree = sl::RBTree<FileCacheUnit, &FileCacheUnit::hook, FcuLess>;

    struct FileCache
    {
        sl::Atomic<size_t> references;
        VfsId id;

        sl::SpinLock lock;
        size_t length;
        FcuTree units;
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
}
