#include <memory/virtual/VfsVmDriver.h>
#include <boot/CommonInit.h>
#include <debug/Log.h>
#include <filesystem/TreeCache.h>
#include <filesystem/FileCache.h>
#include <Memory.h>
#include <Maths.h>

namespace Npk::Memory::Virtual
{
    constexpr size_t FaultMaxMapAhead = 2 * 0x1000;

    void VfsVmDriver::Init(uintptr_t enableFeatures)
    { 
        features.faultHandler = enableFeatures & (uintptr_t)VfsFeature::FaultHandler;

        Log("VmDriver init: vfs, faultHandler=%s", LogLevel::Info, 
            features.faultHandler ? "yes" : "no");
    }

    EventResult VfsVmDriver::HandleFault(VmDriverContext& context, uintptr_t where, VmFaultFlags flags)
    {
        (void)flags;
        using namespace Filesystem;

        auto link = static_cast<VfsVmLink*>(context.range.token);
        ASSERT(link != nullptr, "VFS link is nullptr");
        
        auto node = GetVfsNode(link->node, true);
        VALIDATE_(node.Valid(), { .goodFault = false });
        auto cache = node->cache;
        VALIDATE_(cache.Valid(), { .goodFault = false });

        const FileCacheInfo fcInfo = GetFileCacheInfo();
        const size_t granuleSize = GetHatLimits().modes[fcInfo.hatMode].granularity;
        const HatFlags hatFlags = ConvertFlags(context.range.flags);

        where = sl::AlignDown(where, granuleSize);
        const size_t mapLength = sl::Min(FaultMaxMapAhead, context.range.Top() - where);
        const uintptr_t mappingOffset = where - context.range.base;

        sl::ScopedLock scopeLock(context.lock);
        auto cachePart = GetFileCacheUnit(cache, where - context.range.base + link->fileOffset);
        VALIDATE_(cachePart.Valid(), { .goodFault = false });

        for (size_t i = 0; i < mapLength; i += granuleSize)
        {
            if (i % fcInfo.unitSize == 0)
            {
                cachePart = GetFileCacheUnit(cache, link->fileOffset + mappingOffset + i);
                VALIDATE_(cachePart.Valid(), { .goodFault = false });
            }

            Map(context.map, context.range.base + mappingOffset + i, cachePart->physBase + ((i + mappingOffset + link->fileOffset) % fcInfo.unitSize), 
                fcInfo.hatMode, hatFlags, false);
            context.stats.fileResidentSize += granuleSize;
        }

        return { .goodFault = true };
    }

    QueryResult VfsVmDriver::Query(size_t length, VmFlags flags, uintptr_t attachArg)
    {
        using namespace Filesystem;
        auto arg = reinterpret_cast<const VmFileArg*>(attachArg);

        //do a tentative check that the file exists, and is actually a file.
        //NOTE: this looks like a toctou bug, but its only an optimization - the authoratative check
        //is performed in Attach().
        VfsId fileId { .driverId = 0, .vnodeId = 0 };
        if (arg->id.driverId != 0)
            fileId = arg->id;
        else if (!arg->filepath.Empty())
        {
            auto maybeId = VfsLookup(arg->filepath);
            if (maybeId)
                fileId = *maybeId;
        }
        if (fileId.driverId == 0)
            return { .success = false };

        auto attribs = VfsGetAttribs(fileId);
        if (!attribs.HasValue() || attribs->type != NodeType::File)
            return { .success = false };

        QueryResult result;
        result.success = true;
        result.hatMode = GetFileCacheInfo().hatMode;
        result.alignment = GetHatLimits().modes[result.hatMode].granularity;
        result.length = sl::AlignUp(length, result.alignment);
        
        if (flags.Has(VmFlag::Guarded))
            result.length += 2 * result.alignment;

        return result;
    }

    bool VfsVmDriver::ModifyRange(VmDriverContext& context, ModifyRangeArgs args)
    {
        (void)context; (void)args;
        ASSERT_UNREACHABLE();
    }

    SplitResult VfsVmDriver::Split(VmDriverContext& context, size_t offset)
    {
        (void)context; (void)offset;
        //TODO:
        ASSERT_UNREACHABLE();
    }

    AttachResult VfsVmDriver::Attach(VmDriverContext& context, const QueryResult& query, uintptr_t attachArg)
    {
        using namespace Filesystem;
        auto arg = reinterpret_cast<const VmFileArg*>(attachArg);

        VfsId fileId { .driverId = 0, .vnodeId = 0 };
        if (arg->id.driverId != 0)
            fileId = arg->id;
        else if (!arg->filepath.Empty())
        {
            auto maybeId = VfsLookup(arg->filepath);
            if (maybeId)
                fileId = *maybeId;
        }
        if (fileId.driverId == 0)
            return { .success = false };

        auto node = GetVfsNode(fileId, true);
        if (!node.Valid() || node->type != NodeType::File)
            return { .success = false };

        auto cache = node->cache;
        VfsVmLink* link = new VfsVmLink();
        link->isReadonly = !context.range.flags.Has(VmFlag::Write);
        link->isPrivate = false; //TODO: private mappings
        link->node = fileId;
        link->fileOffset = arg->offset;

        const FileCacheInfo fcInfo = GetFileCacheInfo();
        const size_t granuleSize = GetHatLimits().modes[query.hatMode].granularity;
        const AttachResult result
        {
            .token = link,
            .offset = arg->offset % granuleSize,
            .success = true,
        };
        context.stats.fileWorkingSize += context.range.length - result.offset;

        //we found the file and were able to acquire it's cache, next step depends on our backing strategy.
        //if we're mapping on a page fault then we can exit now. Otherwise map the entire
        //file cache contents.
        if (features.faultHandler && !arg->noDeferBacking)
            return result;

        const HatFlags hatFlags = ConvertFlags(context.range.flags);

        sl::ScopedLock scopeLock(context.lock);
        for (size_t i = 0; i < context.range.length; i += granuleSize)
        {
            auto handle = GetFileCacheUnit(cache, arg->offset + i);
            if (!handle.Valid())
                break;

            Map(context.map, context.range.base + i, handle->physBase + (i % fcInfo.unitSize), 
                query.hatMode, hatFlags, false);
            context.stats.fileResidentSize += granuleSize;
        }

        return result;
    }

    bool VfsVmDriver::Detach(VmDriverContext& context)
    {
        context.stats.fileWorkingSize -= context.range.length - context.range.offset;
        using namespace Filesystem;
        const FileCacheInfo fcInfo = GetFileCacheInfo();

        VfsVmLink* link = static_cast<VfsVmLink*>(context.range.token);

        const size_t granuleSize = GetHatLimits().modes[fcInfo.hatMode].granularity;
        size_t mode;
        size_t phys;

        sl::ScopedLock scopeLock(context.lock);
        for (size_t i = 0; i < context.range.length; i += granuleSize)
        {
            //TODO: if page has dirty bit set, we'll need to mark the file cache entry as dirtied as well
            if (Unmap(context.map, context.range.base + i, phys, mode, true))
                context.stats.fileResidentSize -= granuleSize;
        }

        delete link;
        return true;
    }
}
