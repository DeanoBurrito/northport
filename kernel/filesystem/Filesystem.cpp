#include <filesystem/Filesystem.h>
#include <filesystem/TempFs.h>
#include <filesystem/FileCache.h>
#include <filesystem/TreeCache.h>
#include <filesystem/InitDisk.h>
#include <debug/Log.h>
#include <drivers/DriverManager.h>
#include <memory/Heap.h>
#include <formats/Url.h>

namespace Npk::Filesystem
{
    struct VfsMountInfo
    {
        VfsId occludedDir;
        size_t driverId;
    };

    sl::RwLock mountpointsLock;
    sl::Vector<VfsMountInfo> mountpoints;
    sl::Handle<Drivers::DeviceApi> rootFs;
    VfsId rootNodeId;

    void PrintNode(VfsId id, size_t indent)
    {
        constexpr const char* NodeTypeStrs[] = { "file", "dir", "link/mount" };
        auto node = GetVfsNode(id, true);
        ASSERT_(node.Valid());
        id = node->id;

        auto attribs = VfsGetAttribs(id);
        if (attribs.HasValue())
        {
            Log("%*lu.%lu %s %s", LogLevel::Debug, (int)indent * 2, id.driverId, id.vnodeId, 
                attribs->name.C_Str(), NodeTypeStrs[(size_t)attribs->type]);
        }
        else
        {
            Log("%*lu.%lu <no-name> %s", LogLevel::Debug, (int)indent * 2, id.driverId, id.vnodeId,
                NodeTypeStrs[(size_t)node->type]);
        }

        if (node->type != NodeType::Directory)
            return;
        auto entries = VfsReadDir(id);
        ASSERT_(entries.HasValue());
        for (size_t i = 0; i < entries->children.Size(); i++)
            PrintNode(entries->children[i].id, indent + 1);
    }

    void InitVfs()
    {
        InitTreeCache();
        InitFileCache();

        const size_t rootFsId = CreateTempFs("root tempfs");
        rootFs = Drivers::DriverManager::Global().GetApi(rootFsId);
        auto* rootApi = reinterpret_cast<npk_filesystem_device_api*>(rootFs->api);
        rootNodeId = { .driverId = rootFsId, .vnodeId = rootApi->get_root(rootFs->api) };

        auto& rootMountInfo = mountpoints.EmplaceBack();
        rootMountInfo.driverId = rootFsId;
        rootMountInfo.occludedDir = VfsId { .driverId = 0, .vnodeId = 0 };

        Log("VFS initialized, no root filesystem found.", LogLevel::Info);

        TryLoadInitdisk();
    }

    sl::Opt<VfsId> VfsLookup(sl::StringSpan filepath)
    {
        if (filepath.Empty() || filepath[0] != '/')
            return {};
        
        const sl::Url url = sl::Url::Parse(filepath);
        sl::StringSpan segment = url.GetNextSeg();
        auto selectedNode = GetVfsNode(rootNodeId, true);

        while (!segment.Empty())
        {
            if (!selectedNode.Valid())
                return {};
            auto childId = VfsFindChild(selectedNode->id, segment);
            if (!childId.HasValue())
                return {};

            selectedNode = GetVfsNode(*childId, true);
            segment = url.GetNextSeg(segment);
        }

        return selectedNode->id;
    }

    sl::String VfsGetPath(VfsId id)
    {
        sl::Vector<sl::String> ancestorNames;
        size_t pathLength = 1;
        
        while (true)
        {
            auto attribs = VfsGetAttribs(id);
            if (!attribs.HasValue())
                break;
            ancestorNames.PushBack(attribs->name);
            pathLength++;
            pathLength += attribs->name.Size();
            
            auto parent = VfsFindChild(id, "..");
            if (!parent)
                break;

            if (id.driverId == parent->driverId && id.vnodeId == parent->vnodeId)
                return {}; //node references itself, gtfo
            id = *parent;
        }

        char* buffer = new char[pathLength];
        size_t bufferIndex = pathLength;
        for (size_t i = 0; i < ancestorNames.Size(); i++)
        {
            buffer[--bufferIndex] = '/';
            bufferIndex -= ancestorNames[i].Size();
            sl::memcopy(ancestorNames[i].C_Str(), buffer + bufferIndex, ancestorNames[i].Size());
        }
        buffer[--bufferIndex] = '/';
        buffer[pathLength - 1] = 0;
        ASSERT(bufferIndex == 0, "Unexpected path length");

        return sl::String(buffer, true);
    }

    static bool MountingConditionsMet(sl::Handle<VfsNode, sl::NoHandleDtor> node, sl::Handle<Drivers::DeviceApi> driver)
    {
        VALIDATE_(node->type == NodeType::Directory, false);
        VALIDATE_(node->bond.driverId == 0 && node->bond.vnodeId == 0, false);
        auto nodeDriver = Drivers::DriverManager::Global().GetApi(node->id.driverId);
        auto* fsApi = reinterpret_cast<const npk_filesystem_device_api*>(driver->api);
        size_t childCount = 0;

        npk_fs_context context {};
        context.node_id = node->id.vnodeId;
        context.api = nodeDriver->api;
        context.node_data = node->driverData;
        VALIDATE_(fsApi->read_dir(context, &childCount, nullptr), false);
        VALIDATE_(childCount == 0, false);

        VALIDATE_(driver->api->type == npk_device_api_type::Filesystem, false);

        return true;
    }

    bool VfsMount(VfsId mountpoint, size_t fsDriverId)
    {
        auto target = GetVfsNode(mountpoint, true);
        VALIDATE_(target.Valid(), false);
        mountpoint= target->id;
        auto driver = Drivers::DriverManager::Global().GetApi(fsDriverId);
        VALIDATE_(driver.Valid(), false);

        target->metadataLock.WriterLock();
        if (!MountingConditionsMet(target, driver))
        {
            target->metadataLock.WriterUnlock();
            return false;
        }

        auto* fsApi = reinterpret_cast<const npk_filesystem_device_api*>(driver->api);
        if (!fsApi->mount(driver->api))
        {
            target->metadataLock.WriterUnlock();
            return false;
        }

        target->type = NodeType::Link;
        target->bond.driverId = driver->api->id;
        auto* driverApi = reinterpret_cast<npk_filesystem_device_api*>(driver->api);
        target->bond.vnodeId = driverApi->get_root(driver->api);
        target->metadataLock.WriterUnlock();

        mountpointsLock.WriterLock();
        auto& mountInfo = mountpoints.EmplaceBack();
        mountInfo.driverId = fsDriverId;
        mountInfo.occludedDir = mountpoint;
        mountpointsLock.WriterUnlock();

        const sl::String mountpointName = VfsGetPath(mountpoint);
        Log("Mounted filesystem %lu at %lu.%lu (%s)", LogLevel::Info, target->bond.driverId, 
            mountpoint.driverId, mountpoint.vnodeId, mountpointName.C_Str());
        return true;
    }

    sl::Opt<VfsId> VfsCreate(VfsId dir, NodeType type, sl::StringSpan name)
    {
        while (!name.Empty() && name[name.Size() - 1] == 0)
            name = name.Subspan(0, name.Size() - 1);

        auto parent = GetVfsNode(dir, true);
        VALIDATE_(parent.Valid(), {});
        dir = parent->id;
        auto driver = Drivers::DriverManager::Global().GetApi(dir.driverId);
        VALIDATE_(driver.Valid(), {});

        parent->metadataLock.WriterLock();
        if (parent->type != NodeType::Directory)
        {
            parent->metadataLock.WriterUnlock();
            return {};
        }
        
        //everything checks out, call the filesystem driver and ask it to actually create the file.
        auto* api = reinterpret_cast<npk_filesystem_device_api*>(driver->api);
        npk_fs_context context {};
        context.api = driver->api;
        context.node_id= dir.vnodeId;
        context.node_data = parent->driverData;
        const npk_string apiName = { .length = name.Size(), .data = name.Begin() };

        const npk_handle handle = api->create(context, static_cast<npk_fsnode_type>(type), apiName);
        parent->metadataLock.WriterUnlock();

        if (handle == NPK_INVALID_HANDLE)
            return {};
        return VfsId { .driverId = dir.driverId, .vnodeId = handle };
    }

    bool VfsRemove(VfsId dir, VfsId node)
    {
        auto parent = GetVfsNode(dir, true);
        VALIDATE_(parent.Valid(), {});

        ASSERT_UNREACHABLE();
    }

    sl::Opt<VfsId> VfsFindChild(VfsId dir, sl::StringSpan name)
    {
        while (!name.Empty() && name[name.Size() - 1] == 0)
            name = name.Subspan(0, name.Size() - 1);

        VALIDATE_(!name.Empty(), {});
        auto dirNode = GetVfsNode(dir, true);
        VALIDATE_(dirNode.Valid(), {});

        auto driver = Drivers::DriverManager::Global().GetApi(dirNode->id.driverId);
        VALIDATE_(driver.Valid(), {});
        auto* fsApi = reinterpret_cast<const npk_filesystem_device_api*>(driver->api);

        if (name.Size() == 2 && name[0] == '.' && name[1] == '.' &&
            dir.vnodeId == fsApi->get_root(driver->api))
        {
            //TODO: there's a more elegant solution to this surely
            //special case: accessing child ".." on the root node of the filesystem
            sl::Opt<VfsId> parentId {};
            mountpointsLock.ReaderLock();
            for (size_t i = 0; i < mountpoints.Size(); i++)
            {
                if (mountpoints[i].driverId != driver->api->id)
                    continue;

                if (mountpoints[i].occludedDir.driverId != 0)
                    parentId = mountpoints[i].occludedDir;
                break;
            }
            mountpointsLock.ReaderUnlock();

            if (!parentId.HasValue())
                return {};

            //re-acquire node and driver handles, since we've changed drivers
            dir = *parentId;
            dirNode = GetVfsNode(dir, false);
            VALIDATE_(dirNode.Valid(), {});
            driver = Drivers::DriverManager::Global().GetApi(dirNode->id.driverId);
            VALIDATE_(driver.Valid(), {});
            fsApi = reinterpret_cast<const npk_filesystem_device_api*>(driver->api);
        }
        dir = dirNode->id;

        dirNode->metadataLock.ReaderLock();
        const npk_string apiName { .length = name.Size(), .data = name.Begin() };
        npk_fs_context context {};
        context.api = driver->api;
        context.node_id = dirNode->id.vnodeId;
        context.node_data = dirNode->driverData;

        const npk_handle found = fsApi->find_child(context, apiName);

        dirNode->metadataLock.ReaderUnlock();
        if (found == NPK_INVALID_HANDLE)
            return {};
        return VfsId { .driverId = driver->api->id, .vnodeId = found };
    }

    sl::Opt<NodeAttribs> VfsGetAttribs(VfsId node)
    {
        auto vfsNode = GetVfsNode(node, false);
        VALIDATE_(vfsNode.Valid(), {});
        auto driver = Drivers::DriverManager::Global().GetApi(vfsNode->id.driverId);
        VALIDATE_(driver.Valid(), {});

        auto* fsApi = reinterpret_cast<const npk_filesystem_device_api*>(driver->api);
        if (node.vnodeId == fsApi->get_root(driver->api))
        {
            //special case: root node of filesystem, we want to return the attribs of the mountpoint (occluded node)
            sl::Opt<VfsId> occludedId {};
            mountpointsLock.ReaderLock();
            for (size_t i = 0; i < mountpoints.Size(); i++)
            {
                if (mountpoints[i].driverId != driver->api->id)
                    continue;
                
                if (mountpoints[i].occludedDir.driverId != 0)
                    occludedId = mountpoints[i].occludedDir;
                break;
            }
            mountpointsLock.ReaderUnlock();
            if (!occludedId.HasValue())
                return {};

            node = *occludedId;
            vfsNode = GetVfsNode(node, false);
            VALIDATE_(vfsNode.Valid(), {});
            driver = Drivers::DriverManager::Global().GetApi(vfsNode->id.driverId);
            VALIDATE_(driver.Valid(), {});
            fsApi = reinterpret_cast<const npk_filesystem_device_api*>(driver->api);
        }

        npk_fs_context context {};
        context.api = driver->api;
        context.node_id = vfsNode->id.vnodeId;
        context.node_data = vfsNode->driverData;
        npk_fs_attribs attribStore {};

        vfsNode->metadataLock.ReaderLock();

        if (!fsApi->get_attribs(context, &attribStore))
        {
            vfsNode->metadataLock.ReaderUnlock();
            return {};
        }
        vfsNode->metadataLock.ReaderUnlock();

        NodeAttribs attribs {};
        attribs.type = vfsNode->type;
        attribs.size = attribStore.size;
        attribs.name = sl::StringSpan(attribStore.name.data, attribStore.name.length);

        return attribs;
    }

    bool VfsSetAttribs(VfsId node, const NodeAttribs& attribs, NodeAttribFlags selected)
    {
        auto vfsNode = GetVfsNode(node, true);
        VALIDATE_(vfsNode.Valid(), false);
        node = vfsNode->id;
        auto driver = Drivers::DriverManager::Global().GetApi(node.driverId);
        VALIDATE_(driver.Valid(), false);
        auto* fsApi = reinterpret_cast<const npk_filesystem_device_api*>(driver->api);

        npk_fs_attribs apiAttribs {};
        apiAttribs.size = attribs.size;
        apiAttribs.name = npk_string { .length = attribs.name.Size(), .data = attribs.name.C_Str() };
        const auto apiFlags = static_cast<const npk_fs_attrib_flags>(selected.Raw());
        
        npk_fs_context context {};
        context.api = driver->api;
        context.node_id = vfsNode->id.vnodeId;
        context.node_data = vfsNode->driverData;

        vfsNode->metadataLock.WriterLock();
        if (!fsApi->set_attribs(context, &apiAttribs, apiFlags))
        {
            vfsNode->metadataLock.WriterUnlock();
            return false;
        };

        vfsNode->metadataLock.WriterUnlock();
        if (vfsNode->type == NodeType::File && selected.Has(NodeAttribFlag::Size))
            SetFileCacheLength(vfsNode->cache, attribs.size);
        return true;
    }

    sl::Opt<DirEntries> VfsReadDir(VfsId dir)
    {
        auto node = GetVfsNode(dir, true);
        VALIDATE_(node.Valid(), {});
        dir = node->id;
        auto driver = Drivers::DriverManager::Global().GetApi(dir.driverId);
        VALIDATE_(driver.Valid(), {});

        node->metadataLock.ReaderLock();
        if (node->type != NodeType::Directory)
        {
            node->metadataLock.ReaderUnlock();
            return {};
        }

        VALIDATE_(node->type == NodeType::Directory, {});
        auto* fsApi = reinterpret_cast<const npk_filesystem_device_api*>(driver->api);

        size_t childCount = 0;
        npk_dir_entry* entries = nullptr;
        npk_fs_context context {};
        context.api = driver->api;
        context.node_id = dir.vnodeId;
        context.node_data = node->driverData;

        if (!fsApi->read_dir(context, &childCount, &entries))
        {
            node->metadataLock.ReaderUnlock();
            return {};
        }

        DirEntries listing {};
        listing.children.EnsureCapacity(childCount);
        for (size_t i = 0; i < childCount; i++)
        {
            auto& elem = listing.children.EmplaceBack();
            elem.id = VfsId { .driverId = entries[i].id.device_api, .vnodeId = entries[i].id.node_id };
        }
        Npk::Memory::Heap::Global().Free(entries, childCount * sizeof(npk_dir_entry));

        return listing;
    }
}
