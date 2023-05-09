#include <filesystem/FileCache.h>
#include <arch/Hat.h>
#include <arch/Platform.h>
#include <debug/Log.h>
#include <memory/Pmm.h>
#include <UnitConverter.h>
#include <Maths.h>

namespace Npk::Filesystem
{
    size_t cacheUnitSize;
    
    void InitFileCache()
    {
        //find the smallest granularity supported by the MMU, we'll use that as a cache unit size.
        cacheUnitSize = -1ul;
        const HatLimits& mmuLimits = GetHatLimits();
        for (size_t i = 0; i < mmuLimits.modeCount; i++)
        {
            if (mmuLimits.modes[i].granularity < cacheUnitSize)
                cacheUnitSize = mmuLimits.modes[i].granularity;
        }
        ASSERT(sl::IsPowerOfTwo(cacheUnitSize), "Bad cache unit size");

        auto conv = sl::ConvertUnits(cacheUnitSize, sl::UnitBase::Binary);
        Log("File cache initialized: unitSize=0x%lx (%lu.%lu%sB)", LogLevel::Info,
            cacheUnitSize, conv.major, conv.minor, conv.prefix);
    }

    size_t FileCacheUnitSize()
    { return cacheUnitSize; }

    sl::Handle<FileCacheUnit> GetFileCache(FileCache* cache, size_t offset, bool createNew)
    {
        VALIDATE(cache != nullptr, {}, "FileCache is nullptr");
        
        offset = sl::AlignDown(offset, cacheUnitSize);
        for (auto it = cache->units.Begin(); it != cache->units.End(); ++it)
        {
            if (offset == it->offset)
                return &*it;
        }

        if (!createNew)
            return {};
        FileCacheUnit* unit = &cache->units.EmplaceBack();
        unit->references = 1;
        unit->offset = offset;
        unit->physBase = (void*)PMM::Global().Alloc(cacheUnitSize / PageSize);
        ASSERT(unit->physBase != nullptr, "PMM alloc failed.");

        return unit;
    }
}
