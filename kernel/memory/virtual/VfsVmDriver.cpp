#include <memory/virtual/VfsVmDriver.h>
#include <boot/CommonInit.h>
#include <debug/Log.h>
#include <filesystem/Filesystem.h>
#include <filesystem/FileCache.h>
#include <Memory.h>

namespace Npk::Memory::Virtual
{
    void VfsVmDriver::Init(uintptr_t enableFeatures)
    { 
        features.faultHandler = enableFeatures & (uintptr_t)VfsFeature::FaultHandler;

        Log("VmDriver init: vfs, faultHandler=%s", LogLevel::Info, 
            features.faultHandler ? "yes" : "no");
    }

    EventResult VfsVmDriver::HandleFault(VmDriverContext& context, uintptr_t where, VmFaultFlags flags)
    {
        ASSERT_UNREACHABLE()
    }

    QueryResult VfsVmDriver::Query(size_t length, VmFlags flags, uintptr_t attachArg)
    {
        using namespace Filesystem;
        auto arg = reinterpret_cast<const VmoFileInitArg*>(attachArg);

        //do a tentative check that the file exists, and is actually a file.
        //NOTE: this looks like a toctou bug, but its only an optimization - the authoratative check
        //is performed in Attach().
        if (auto file = VfsLookup(arg->filepath, KernelFsCtxt); file.Valid())
        {
            if (file->type != NodeType::File)
                return { .success = false };
        }
        else
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

    bool VfsVmDriver::ModifyRange(VmDriverContext& context, sl::Opt<VmFlags> flags)
    {
        ASSERT_UNREACHABLE()
    }

    AttachResult VfsVmDriver::Attach(VmDriverContext& context, const QueryResult& query, uintptr_t attachArg)
    {
        using namespace Filesystem;
        auto arg = reinterpret_cast<const VmoFileInitArg*>(attachArg);

        auto fileHandle = VfsLookup(arg->filepath, KernelFsCtxt);
        if (!fileHandle.Valid()) //assert whether we obtained a handle to the file or not.
            return { .success = false };
        if (fileHandle->type != NodeType::File)
            return { .success = false };
        if (!fileHandle->Open(KernelFsCtxt))
            return { .success = false };

        FileCache* cache = fileHandle->link.cache;

        VfsVmLink* link = new VfsVmLink();
        link->readonly = !context.range.flags.Has(VmFlag::Write);
        link->vfsNode = sl::Move(fileHandle);
        link->fileOffset = arg->offset;
        (void)fileHandle;

        const AttachResult result 
        {
            .token = link,
            .offset = 0, //TODO: correctly set offset
            .success = true,
        };

        //we found the file and were able to open it, next step depends on our backing strategy.
        //if we're mapping on a page fault then we can exit now. Otherwise map the entire
        //file cache contents.
        if (features.faultHandler && !arg->noDeferBacking)
            return result;

        const size_t granuleSize = GetHatLimits().modes[query.hatMode].granularity;
        const HatFlags hatFlags = ConvertFlags(context.range.flags);

        sl::ScopedLock scopeLock(context.lock);
        for (size_t i = 0; i < context.range.length; i += granuleSize)
        {
            auto handle = GetFileCache(cache, arg->offset + i, !link->readonly);
            if (!handle.Valid())
                break;
            Map(context.map, context.range.base + i, handle->physBase, query.hatMode, hatFlags, false);
        }

        return result;
    }

    bool VfsVmDriver::Detach(VmDriverContext& context)
    {
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
            Unmap(context.map, context.range.base + i, phys, mode, true);
        }

        delete link;
        return true;
    }
}
