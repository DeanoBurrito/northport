#include <filesystem/TempFs.h>
#include <containers/Vector.h>
#include <debug/Log.h>
#include <drivers/DriverManager.h>

namespace Npk::Filesystem
{
    struct TempFsNode
    {
        sl::Atomic<size_t> references;
        sl::RwLock lock;

        TempFsNode* parent;
        sl::Vector<sl::Handle<TempFsNode>> children;
        NodeType type;
        size_t id;
        sl::String name;
    };

    struct TempFsData
    {
        sl::RwLock lock;
        sl::Vector<sl::Handle<TempFsNode>> nodes; //TODO: deque? vector could get a little unweildy
        sl::String tag;
    };

    npk_string TempFsGetSummary(npk_device_api* api)
    {
        auto* data = static_cast<const TempFsData*>(api->driver_data);
        return { .length = data->tag.Size(), .data = data->tag.C_Str() };
    }

    npk_fsnode_type TempFsEnterCache(npk_device_api* api, npk_handle id, void** driver_data)
    {
        id -= 1;
        TempFsData* data = static_cast<TempFsData*>(api->driver_data);
        ASSERT_(id < data->nodes.Size());
        ASSERT_(data->nodes[id].Valid());

        return static_cast<npk_fsnode_type>(data->nodes[id]->type);
    }

    bool TempFsExitCache(npk_device_api* api, npk_handle id, void* data)
    {
        (void)api; (void)id; (void)data;
        return true;
    }

    npk_handle TempFsGetRoot(npk_device_api* api)
    {
        (void)api;
        return 1;
    }

    bool TempFsMount(npk_device_api* api)
    {
        (void)api;
        return true;
    }

    bool TempFsUnmount(npk_device_api* api)
    {
        (void)api;
        return false;
    }

    npk_handle TempFsCreate(npk_device_api* api, npk_handle dir, npk_fsnode_type type, npk_string name)
    {
        dir -= 1;
        TempFsData* data = static_cast<TempFsData*>(api->driver_data);
        ASSERT_(dir < data->nodes.Size());

        data->lock.WriterLock();
        auto parent = data->nodes[dir];
        parent->lock.WriterLock();
        data->lock.WriterUnlock();
        
        //TODO: check for name collisions

        //create new node and link it to parent
        TempFsNode* child = new TempFsNode();
        child->references = 0;

        child->parent = *parent;
        child->type = static_cast<NodeType>(type);
        child->name = sl::StringSpan(name.data, name.length);
        parent->children.PushBack(child);

        //assign a local vnode id for this node
        sl::Opt<npk_handle> id {};
        for (size_t i = 0; i < data->nodes.Size(); i++)
        {
            if (data->nodes[i].Valid())
                continue;
            id = i;
            data->nodes[i] = child;
        }
        if (!id.HasValue())
        {
            id = data->nodes.Size();
            data->nodes.PushBack(child);
        }

        parent->lock.WriterUnlock();
        return *id + 1;
    }

    bool TempFsRemove(npk_device_api* api, npk_handle dir, npk_handle node)
    {
        ASSERT_UNREACHABLE();
    }

    npk_handle TempFsFindChild(npk_device_api* api, npk_handle dir, npk_string name)
    {
        if (name.data == nullptr || name.length == 0)
            return NPK_INVALID_HANDLE;

        dir -= 1;
        TempFsData* data = static_cast<TempFsData*>(api->driver_data);
        ASSERT_(dir < data->nodes.Size());
        ASSERT_(data->nodes[dir].Valid());

        const sl::StringSpan nameSpan(name.data, name.length);
        auto& node = data->nodes[dir];
        for (size_t i = 0; i < node->children.Size(); i++)
        {
            auto& child = node->children[i];
            if (child->name.Span() != nameSpan)
                continue;

            return child->id;
        }

        return NPK_INVALID_HANDLE;
    }

    bool TempFsGetDirListing(npk_device_api* api, npk_handle dir, size_t* count, npk_dir_entry** listing)
    {
        VALIDATE_(count != nullptr, false);

        dir -= 1;
        TempFsData* data = static_cast<TempFsData*>(api->driver_data);
        ASSERT_(dir < data->nodes.Size());
        ASSERT_(data->nodes[dir].Valid());

        *count = data->nodes[dir]->children.Size();
        ASSERT(listing == nullptr, "Not implemented");
        return true;
    }

    npk_filesystem_device_api tempFsApi
    {
        .header =
        {
            .id = 0,
            .type = npk_device_api_type::Filesystem,
            .driver_data = nullptr,
            .get_summary = TempFsGetSummary,
        },
        
        .enter_cache = TempFsEnterCache,
        .exit_cache = TempFsExitCache,
        .get_root = TempFsGetRoot,
        .mount = TempFsMount,
        .unmount = TempFsUnmount,

        .create = TempFsCreate,
        .remove = TempFsRemove,
        .find_child = TempFsFindChild,
        .get_dir_listing = TempFsGetDirListing,
    };

    size_t CreateTempFs(sl::StringSpan tag)
    {
        auto* tempFs = new npk_filesystem_device_api();
        *tempFs = tempFsApi;

        TempFsData* data = new TempFsData();
        tempFs->header.driver_data = data;
        TempFsNode* rootNode = new TempFsNode();
        data->nodes.EmplaceBack(rootNode);
        data->tag = tag;
        rootNode->type = NodeType::Directory;
        rootNode->parent = nullptr;
        rootNode->id = 1;

        ASSERT(Drivers::DriverManager::Global().AddApi(&tempFs->header, true), "Failed to create tempfs.");

        return tempFs->header.id;
    }
}
