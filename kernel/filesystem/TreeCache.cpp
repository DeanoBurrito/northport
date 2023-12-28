#include <filesystem/TreeCache.h>
#include <debug/Log.h>
#include <drivers/DriverManager.h>

namespace Npk::Filesystem
{
    constexpr size_t TreeCacheInitialSize = 128;

    struct CachedNode
    {
        VfsNode node;
        sl::SpinLock busy;
    };

    sl::SpinLock cacheLock;
    sl::Span<CachedNode> cache;

    void InitTreeCache()
    {
        cache = sl::Span<CachedNode>(new CachedNode[TreeCacheInitialSize], TreeCacheInitialSize);

        Log("Vfs node cache initialized: initialSize=%lu (%lu B)", LogLevel::Info, 
            cache.Size(), cache.SizeBytes());
    }

    static sl::Handle<VfsNode, sl::NoHandleDtor> GetVfsNodeInternal(VfsId id)
    {
        if (id.driverId == 0 || id.vnodeId == 0)
            return {};

        sl::ScopedLock scopeLock(cacheLock); //TODO: go lockness oneday? ;)

        //TODO: hashmap or something better for a cache
        for (size_t i = 0; i < cache.Size(); i++)
        {
            if (cache[i].node.id.driverId != id.driverId ||
                cache[i].node.id.vnodeId != id.vnodeId)
                continue;
            
            //TODO: we should honour the busy flag here, and potentially the direction of the cache operation that set it
            return &cache[i].node;
        }

        //node wasnt found in memory, we're going to enter it into the cache.
        //first step is to find a free cache slot, or make one.
        sl::Opt<size_t> allocIndex {};
        for (size_t i = 0; i < cache.Size(); i++)
        {
            if (cache[i].node.references != 0)
                continue;

            if (!allocIndex.HasValue() && cache[i].busy.TryLock())
                allocIndex = i;
            else if (allocIndex.HasValue() && cache[*allocIndex].node.id.vnodeId != 0
                && cache[i].node.id.vnodeId == 0 && cache[i].busy.TryLock())
            {
                cache[*allocIndex].busy.Unlock();
                allocIndex = i;
            }

            if (allocIndex.HasValue() && cache[*allocIndex].node.id.vnodeId == 0)
                break;
        }
        VALIDATE_(allocIndex.HasValue(), {});
        auto& store = cache[*allocIndex];

        if (store.node.id.driverId != 0)
        {
            auto driver = Drivers::DriverManager::Global().GetApi(store.node.id.driverId);
            VALIDATE_(driver.Valid(), {});
            auto* fsApi = reinterpret_cast<const npk_filesystem_device_api*>(driver->api);
            ASSERT_(fsApi->exit_cache(driver->api, store.node.id.driverId, store.node.driverData));
            store.node.driverData = nullptr;
            //TODO: if exit_cache() fails, we should try to continue by finding another usable slot.
        }

        auto driver = Drivers::DriverManager::Global().GetApi(id.driverId);
        VALIDATE_(driver.Valid(), {});
        auto* fsApi = reinterpret_cast<const npk_filesystem_device_api*>(driver->api);

        new(&store.node) VfsNode();
        store.node.metadataLock.WriterLock();
        store.node.id = id;
        store.node.type = static_cast<NodeType>(fsApi->enter_cache(driver->api, id.vnodeId, &store.node.driverData));
        //TODO: populate bond field.
        
        if (store.node.type == NodeType::File)
            store.node.cache = GetFileCache(id);
        store.node.metadataLock.WriterUnlock();

        //taking the handle before we acquire the lock is important
        sl::Handle<VfsNode, sl::NoHandleDtor> handle = &store.node;
        store.busy.Unlock();

        return handle;
    }

    sl::Handle<VfsNode, sl::NoHandleDtor> GetVfsNode(VfsId id, bool traverseLinks)
    {
        auto node = GetVfsNodeInternal(id);
        if (traverseLinks && node.Valid() && node->type == NodeType::Link)
            return GetVfsNodeInternal(node->bond);
        return node;
    }
}
