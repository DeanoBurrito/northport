#include <filesystem/Filesystem.h>
#include <filesystem/TempFs.h>
#include <debug/Log.h>
#include <drivers/DriverManager.h>
#include <formats/Url.h>

namespace Npk::Filesystem
{
    constexpr size_t InitialNodeCacheCapacity = 128;

    struct VfsCachedNode
    {
        VfsNode node;
        void* driverData;
        sl::SpinLock busy;
    };

    sl::Handle<Drivers::DeviceApi> rootFs;
    size_t rootFsId;
    VfsId rootNodeId;
    sl::SpinLock nodeCacheLock;
    sl::Span<VfsCachedNode> nodeCache; //TODO: auto-resizing container like a non-moving dequeue?

    void InitVfs()
    {
        nodeCache = sl::Span<VfsCachedNode>(new VfsCachedNode[InitialNodeCacheCapacity], InitialNodeCacheCapacity);
        rootFsId = CreateTempFs("root tempfs");
        rootFs = Drivers::DriverManager::Global().GetApi(rootFsId);
        auto* rootApi = reinterpret_cast<npk_filesystem_device_api*>(rootFs->api);
        rootNodeId = { .driverId = rootFsId, .vnodeId = rootApi->get_root(rootFs->api) };

        Log("VFS initialized, no root filesystem found.", LogLevel::Info);
    }

    sl::Opt<VfsId> VfsLookup(sl::StringSpan filepath)
    {
        if (filepath.Empty() || filepath[0] != '/')
            return {};
        
        const sl::Url url = sl::Url::Parse(filepath);
        sl::StringSpan segment = url.GetNextSeg();
        auto selectedNode = VfsGetNode(rootNodeId);

        while (!segment.Empty())
        {
            if (!selectedNode.Valid())
                return {};
            auto childId = VfsFindChild(selectedNode->id, segment);
            if (!childId.HasValue())
                return {};

            selectedNode = VfsGetNode(*childId);
            segment = url.GetNextSeg(segment);
        }

        return selectedNode->id;
    }

    sl::Handle<VfsNode, sl::NoHandleDtor> VfsGetNode(VfsId id)
    {
        sl::ScopedLock scopeLock(nodeCacheLock); //TODO: go lockless? ;)

        //TODO: hashmap, or anything better than this
        for (size_t i = 0; i < nodeCache.Size(); i++)
        {
            if (nodeCache[i].node.id.driverId != id.driverId)
                continue;
            if (nodeCache[i].node.id.vnodeId != id.vnodeId)
                continue;

            //TODO: what happens if the busy flag is set for a slot we're returning? Possible use-after-free?
            Log("NodeCache hit: slot %lu, %lu:%lu", LogLevel::Debug, i, id.driverId, id.vnodeId);
            return &nodeCache[i].node;
        }

        //node wasnt found in cache, find a free slot
        sl::Opt<size_t> allocIndex {};
        for (size_t i = 0; i < nodeCache.Size(); i++)
        {
            if (nodeCache[i].node.references != 0)
                continue; //node is in use elsewhere, dont touch it.

            if (!allocIndex.HasValue() && nodeCache[i].busy.TryLock())
                allocIndex = i;
            else if (allocIndex.HasValue() && nodeCache[*allocIndex].node.id.vnodeId != 0
                && nodeCache[i].node.id.vnodeId == 0 && nodeCache[i].busy.TryLock())
            {
                nodeCache[*allocIndex].busy.Unlock();
                allocIndex = i;
            }

            if (allocIndex.HasValue() && nodeCache[*allocIndex].node.id.vnodeId == 0)
                break;
        }
        VALIDATE_(allocIndex.HasValue(), {});
        auto& store = nodeCache[*allocIndex];

        //if slot is already occupied by a node, inform the fs driver it's time to cleanup a little bit
        if (store.node.id.driverId != 0)
        {
            Log("NodeCache eviction: slot %lu, %lu:%lu", LogLevel::Debug, *allocIndex, 
                store.node.id.driverId, store.node.id.vnodeId);
            auto driver = Drivers::DriverManager::Global().GetApi(store.node.id.driverId);
            auto* fsApi = reinterpret_cast<const npk_filesystem_device_api*>(driver->api);
            ASSERT_(fsApi->exit_cache(driver->api, store.node.id.vnodeId, store.driverData));
            store.driverData = nullptr;
            //TODO: handle `exit_cache()` returning false, we should retry the search for a free slot in the cache.
        }

        //load new node into slot
        auto driver = Drivers::DriverManager::Global().GetApi(id.driverId);
        VALIDATE_(driver.Valid(), {});
        auto* fsApi = reinterpret_cast<const npk_filesystem_device_api*>(driver->api);

        new(&store.node) VfsNode();
        store.node.metadataLock.WriterLock();
        store.node.id = id;
        store.node.type = static_cast<NodeType>(fsApi->enter_cache(driver->api, id.vnodeId, &store.driverData));
        //TODO: locate/create a FileCache instance for this node
        //TODO: populate bond field
        store.node.metadataLock.WriterUnlock();

        //acquire a handle to the new node, *then* release the busy lock
        sl::Handle<VfsNode, sl::NoHandleDtor> handle = &store.node;
        store.busy.Unlock();

        Log("NodeCache load: slot %lu, %lu:%lu", LogLevel::Debug, *allocIndex, store.node.id.driverId, store.node.id.vnodeId);
        if (handle->type == NodeType::Link)
            return VfsGetNode(handle->bond);
        return handle;
    }

    sl::String VfsGetPath(VfsId id)
    {
        ASSERT_UNREACHABLE()
    }

    static bool MountingConditionsMet(sl::Handle<VfsNode, sl::NoHandleDtor> node, sl::Handle<Drivers::DeviceApi> driver)
    {
        VALIDATE_(node->type == NodeType::Directory, false);
        VALIDATE_(node->bond.driverId == 0 && node->bond.vnodeId == 0, false);
        auto nodeDriver = Drivers::DriverManager::Global().GetApi(node->id.driverId);
        auto* fsApi = reinterpret_cast<const npk_filesystem_device_api*>(driver->api);
        size_t childCount = 0;
        VALIDATE_(fsApi->get_dir_listing(nodeDriver->api, node->id.vnodeId, &childCount, nullptr), false);
        VALIDATE_(childCount == 0, false);

        VALIDATE_(driver->api->type == npk_device_api_type::Filesystem, false);

        return true;
    }

    bool VfsMount(VfsId mountpoint, size_t fsDriverId)
    {
        auto target = VfsGetNode(mountpoint);
        VALIDATE_(target.Valid(), false);
        auto driver = Drivers::DriverManager::Global().GetApi(fsDriverId);
        VALIDATE_(driver.Valid(), false);

        target->metadataLock.WriterLock();
        if (!MountingConditionsMet(target, driver))
        {
            target->metadataLock.WriterUnlock();
            return false;
        }

        target->type = NodeType::Link;
        target->bond.driverId = driver->api->id;
        auto* driverApi = reinterpret_cast<npk_filesystem_device_api*>(driver->api);
        target->bond.vnodeId = driverApi->get_root(driver->api);
        target->metadataLock.WriterUnlock();

        Log("Mounted filesystem %lu at %lu.%lu", LogLevel::Debug, target->bond.driverId, mountpoint.driverId, mountpoint.vnodeId);
        return true;
    }

    sl::Opt<VfsId> VfsCreate(VfsId dir, NodeType type, sl::StringSpan name)
    {
        auto parent = VfsGetNode(dir);
        VALIDATE_(parent.Valid(), {});
        auto driver = Drivers::DriverManager::Global().GetApi(dir.driverId);
        VALIDATE_(driver.Valid(), {});

        parent->metadataLock.WriterLock();
        VALIDATE_(parent->type == NodeType::Directory, {});
        
        //everything checks out, call the filesystem driver and ask it to actually create the file.
        auto* api = reinterpret_cast<npk_filesystem_device_api*>(driver->api);
        const npk_string apiName = { .length = name.Size(), .data = name.Begin() };
        const npk_handle handle = api->create(&api->header, dir.vnodeId, 
            static_cast<npk_fsnode_type>(type), apiName);
        if (handle == NPK_INVALID_HANDLE)
        {
            parent->metadataLock.WriterUnlock();
            return {};
        }

        parent->metadataLock.WriterUnlock();
        return VfsId { .driverId = dir.driverId, .vnodeId = handle };
    }

    bool VfsRemove(VfsId dir, VfsId node)
    {
        auto parent = VfsGetNode(dir);
        VALIDATE_(parent.Valid(), {});

        ASSERT_UNREACHABLE();
    }

    sl::Opt<VfsId> VfsFindChild(VfsId dir, sl::StringSpan name)
    {
        VALIDATE_(!name.Empty(), {});
        auto dirNode = VfsGetNode(dir);
        VALIDATE_(dirNode.Valid(), {});

        //if starting node is a link, follow it
        dirNode->metadataLock.ReaderLock();
        if (dirNode->type == NodeType::Link)
        {
            auto overlay = VfsGetNode(dirNode->bond);
            dirNode->metadataLock.ReaderUnlock();
            overlay->metadataLock.ReaderLock();
            dirNode = overlay;
        }
        (void)dir; //this ID may be incorrect after following a link

        auto driver = Drivers::DriverManager::Global().GetApi(dirNode->id.driverId);
        VALIDATE_(driver.Valid(), {});
        auto* fsApi = reinterpret_cast<npk_filesystem_device_api*>(driver->api);
        const npk_string apiName { .length = name.Size(), .data = name.Begin() };
        const npk_handle found = fsApi->find_child(driver->api, dirNode->id.vnodeId, apiName);

        dirNode->metadataLock.ReaderUnlock();
        if (found == NPK_INVALID_HANDLE)
            return {};
        return VfsId { .driverId = driver->api->id, .vnodeId = found };
    }

    sl::Opt<NodeAttribs> VfsGetAttribs(VfsId node)
    {
        ASSERT_UNREACHABLE();
    }

    sl::Opt<DirListing> VfsGetDirListing(VfsId node)
    {
        ASSERT_UNREACHABLE();
    }
}
