#include <filesystem/FileCache.h>
#include <arch/Hat.h>
#include <arch/Platform.h>
#include <debug/Log.h>
#include <memory/Pmm.h>
#include <memory/virtual/VfsVmDriver.h>
#include <UnitConverter.h>
#include <Memory.h>
#include <Maths.h>

namespace Npk::Filesystem
{
    FileCacheInfo cacheInfo;

    void InitFileCache()
    {
        //find the smallest granularity supported by the MMU, we'll use that as a cache unit size.
        cacheInfo.unitSize = -1ul;
        const HatLimits& mmuLimits = GetHatLimits();
        for (size_t i = 0; i < mmuLimits.modeCount; i++)
        {
            if (mmuLimits.modes[i].granularity < cacheInfo.unitSize)
            {
                cacheInfo.unitSize = mmuLimits.modes[i].granularity;
                cacheInfo.hatMode = i;
                cacheInfo.modeMultiple = 4;
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

    sl::Handle<FileCacheUnit> GetFileCache(FileCache* cache, size_t offset, bool createNew)
    {
        VALIDATE(cache != nullptr, {}, "FileCache is nullptr");
        
        offset = sl::AlignDown(offset, cacheInfo.unitSize);
        for (auto it = cache->units.Begin(); it != cache->units.End(); ++it)
        {
            if (offset == it->offset)
                return &*it;
        }

        if (!createNew) //readonly lookup, return an empty handle
            return {};
        
        FileCacheUnit* unit = &cache->units.EmplaceBack();
        unit->offset = offset;
        const uintptr_t physAddr = PMM::Global().Alloc(cacheInfo.unitSize / PageSize);
        ASSERT(physAddr != 0, "PMM alloc failed.");
        unit->physBase = physAddr;

        sl::memset(reinterpret_cast<void*>(physAddr + hhdmBase), 0, cacheInfo.unitSize);

        //link this page's metadata to the filecache
        Memory::PageInfo* info = PMM::Global().Lookup(physAddr);
        info->link = (uintptr_t)cache;

        return unit;
    }
}
