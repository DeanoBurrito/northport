#include <filesystem/FileCache.h>
#include <arch/Hat.h>
#include <debug/Log.h>
#include <memory/Pmm.h>
#include <UnitConverter.h>

namespace Npk::Filesystem
{
    FileCacheInfo cacheInfo;

    sl::SpinLock cachesLock;
    sl::Vector<sl::Handle<FileCache>> caches; //TODO: better cache than this

    void CleanupFileCacheUnit(FileCacheUnit* unit)
    {
        ASSERT_(unit->owner == nullptr);
        ASSERT_(unit->references == 0);

        auto* memInfo = PMM::Global().Lookup(unit->physBase);
        memInfo->flags.SetBits(Memory::PmFlags::Busy);
        //TODO: ensure unit is not marked dirty.

        PMM::Global().Free(unit->physBase, cacheInfo.unitSize / PageSize);
        delete unit;
    }

    void InitFileCache()
    {
        //find the smallest granularity supported by the MMU, we'll use that as a cache unit size.
        cacheInfo.unitSize = -1ul;
        const HatLimits& mmuLimits = GetHatLimits();
        for (size_t i = 0; i < mmuLimits.modeCount; i++)
        {
            if (mmuLimits.modes[i].granularity < cacheInfo.unitSize)
            {
                cacheInfo.hatMode = i;
                cacheInfo.modeMultiple = 4;
                cacheInfo.unitSize = mmuLimits.modes[i].granularity * cacheInfo.modeMultiple;
            }
        }
        ASSERT(cacheInfo.unitSize != -1ul, "Unable to select cache unit size");
        ASSERT(sl::IsPowerOfTwo(cacheInfo.unitSize), "Bad cache unit size");

        auto conv = sl::ConvertUnits(cacheInfo.unitSize, sl::UnitBase::Binary);
        Log("File cache initialized: unitSize=0x%lx (%lu.%lu%sB)", LogLevel::Info,
            cacheInfo.unitSize, conv.major, conv.minor, conv.prefix);
    }

    FileCacheInfo GetFileCacheInfo()
    { return cacheInfo; }

    sl::Handle<FileCache> GetFileCache(VfsId id)
    {
        sl::ScopedLock scopeLock(cachesLock);

        for (size_t i = 0; i < caches.Size(); i++)
        {
            if (caches[i]->id.driverId != id.driverId)
                continue;
            if (caches[i]->id.vnodeId != id.vnodeId)
                continue;

            return caches[i];
        }

        auto cache = caches.EmplaceBack(new FileCache());
        cache->id = id;
        cache->length = 0;
        cache->references++; //TODO: remove this pin
        return cache;
    }

    bool SetFileCacheLength(sl::Handle<FileCache> cache, size_t length)
    {
        VALIDATE_(cache.Valid(), false);

        sl::ScopedLock scopeLock(cache->lock);
        sl::Swap(cache->length, length);
        if (length <= cache->length)
            return true; //extending file, nothing more to do

        //reducing file length, drop cache units and free memroy
        for (auto it = cache->units.Begin(); it != cache->units.End(); ++it)
        {
            auto handle = *it;
            if (handle->offset < cache->length)
                continue;

            handle->owner= nullptr;
            cache->units.Erase(it);
        }

        return true;
    }

    void CreateFileCacheEntries(sl::Handle<FileCache> fileCache, uintptr_t paddr, size_t size) {
    }

    FileCacheUnitHandle GetFileCacheUnit(sl::Handle<FileCache> cache, size_t fileOffset)
    {
        VALIDATE_(cache.Valid(), {});
        fileOffset = sl::AlignDown(fileOffset, cacheInfo.unitSize);
        VALIDATE_(fileOffset < cache->length, {});

        sl::ScopedLock scopeLock(cache->lock);
        for (auto it = cache->units.Begin(); it != cache->units.End(); ++it)
        {
            auto handle = *it;
            if (fileOffset == handle->offset)
                return handle;
        }

        //TODO: cache miss, here we just create a blank one, but we should also fetch from disk in the future.
        const uintptr_t physicalMem = PMM::Global().Alloc(cacheInfo.unitSize / PageSize);
        VALIDATE_(physicalMem != 0, {});

        auto newUnit = cache->units.EmplaceBack(new FileCacheUnit());
        newUnit->offset = fileOffset;
        newUnit->physBase = physicalMem;
        newUnit->owner = *cache;

        Memory::PageInfo* info = PMM::Global().Lookup(physicalMem);
        info->link = reinterpret_cast<uintptr_t>(*cache);

        return newUnit;
    }
}
