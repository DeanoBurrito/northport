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
        ASSERT_(!unit->flags.Has(FcuFlag::Dirty)); //TODO: flush to backing storage

        auto* memInfo = PMM::Global().Lookup(unit->physBase);
        memInfo->flags.SetBits(Memory::PmFlags::Busy);

        PMM::Global().Free(unit->physBase, cacheInfo.unitSize / PageSize);
        delete unit;
    }

    void InitFileCache()
    {
        //find the smallest granularity supported by the MMU, we'll use that as a cache unit size.
        cacheInfo.unitSize = -1ul;
        const HatLimits& mmuLimits = HatGetLimits();
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
        Log("File cache initialized: unitSize=0x%zx (%zu.%zu%sB)", LogLevel::Info,
            cacheInfo.unitSize, conv.major, conv.minor, conv.prefix);
    }

    FileCacheInfo GetFileCacheInfo()
    { return cacheInfo; }

    sl::Handle<FileCache> GetFileCache(VfsId id)
    {
        //ensure that filesystem is allowed to be cached
        auto mountOpts = VfsGetMountOptions(id.driverId);
        VALIDATE_(mountOpts.HasValue(), {});
        VALIDATE_(!mountOpts->uncachable, {});

        sl::ScopedLock scopeLock(cachesLock);

        for (size_t i = 0; i < caches.Size(); i++)
        {
            if (caches[i]->id.driverId != id.driverId)
                continue;
            if (caches[i]->id.vnodeId != id.vnodeId)
                continue;

            return caches[i];
        }

        //cache for file not found, create a new one
        auto cache = caches.EmplaceBack(new FileCache());
        cache->id = id;
        cache->length = 0;
        cache->references = 1; //TODO: remove this pin

        return cache;
    }

    static void TrimFcuNode(FileCacheUnit* fcu)
    {
        if (auto left = FcuTree::GetLeft(fcu); left != nullptr)
            TrimFcuNode(left);
        if (auto right = FcuTree::GetRight(fcu); right != nullptr)
            TrimFcuNode(right);

        fcu->owner = nullptr;
        fcu->references--;
    }

    bool SetFileCacheLength(sl::Handle<FileCache> cache, size_t length)
    {
        VALIDATE_(cache.Valid(), false);

        sl::ScopedLock scopeLock(cache->lock);
        sl::Swap(cache->length, length);
        if (length <= cache->length)
            return true; //extending file, nothing more to do

        //reducing a file's length, drop references to any cache units beyond new size
        FileCacheUnit* scan = cache->units.GetRoot();
        while (scan != nullptr)
        {
            if (scan->offset < cache->length)
            {
                scan = FcuTree::GetRight(scan);
                continue;
            }

            TrimFcuNode(scan);
            scan = cache->units.GetRoot();
        }

        return true;
    }

    FileCacheUnitHandle GetFileCacheUnit(sl::Handle<FileCache> cache, size_t fileOffset)
    {
        VALIDATE_(cache.Valid(), {});
        fileOffset = sl::AlignDown(fileOffset, cacheInfo.unitSize);
        VALIDATE_(fileOffset < cache->length, {});

        sl::ScopedLock scopeLock(cache->lock);
        FileCacheUnit* scan = cache->units.GetRoot();
        while (scan != nullptr)
        {
            if (fileOffset >= scan->offset && fileOffset < scan->offset + cacheInfo.unitSize)
                return scan;

            if (fileOffset < scan->offset)
                scan = cache->units.GetLeft(scan);
            else
                scan = cache->units.GetRight(scan);
        }
        
        //TODO: cache miss, here we just create a blank one, but we should also fetch from disk in the future.
        auto mountOpts = VfsGetMountOptions(cache->id.driverId);
        VALIDATE_(mountOpts.HasValue(), {});
        const uintptr_t physicalMem = PMM::Global().Alloc(cacheInfo.unitSize / PageSize);
        VALIDATE_(physicalMem != 0, {});

        auto newUnit = new FileCacheUnit();
        newUnit->offset = fileOffset;
        newUnit->physBase = physicalMem;
        newUnit->owner = *cache;
        newUnit->references = 1; //TODO: remove this pin
        if (mountOpts->writable)
            newUnit->flags.Set(FcuFlag::Writable);

        cache->units.Insert(newUnit);
        Memory::PageInfo* info = PMM::Global().Lookup(physicalMem);
        info->link = reinterpret_cast<uintptr_t>(*cache);

        return newUnit;
    }
}
