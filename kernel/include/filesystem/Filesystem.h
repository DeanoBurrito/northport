#pragma once

#include <Locks.h>
#include <Optional.h>
#include <containers/Vector.h>
#include <Flags.h>
#include <String.h>

namespace Npk::Filesystem
{
    struct VfsId
    {
        size_t driverId;
        size_t vnodeId;
    };

    enum class NodeType : size_t
    {
        File = 0,
        Directory = 1,
        Link = 2,
    };

    struct NodeAttribs
    {
        NodeType type; //duplicate of node field in node struct, here for accessibility
        size_t size;
        sl::String name;
    };

    enum class NodeAttribFlag : size_t
    {
        Size = 0,
        Name = 1,
        Caps = 2,
    };

    using NodeAttribFlags = sl::Flags<NodeAttribFlag>;

    struct DirEntry
    {
        VfsId id;
    };

    struct DirEntries
    {
        sl::Vector<DirEntry> children;
    };

    void PrintNode(VfsId id, size_t indent);

    void InitVfs();
    sl::Opt<VfsId> VfsLookup(sl::StringSpan filepath);
    sl::String VfsGetPath(VfsId id);

    bool VfsMount(VfsId mountpoint, size_t fsDriverId);
    sl::Opt<VfsId> VfsCreate(VfsId dir, NodeType type, sl::StringSpan name);
    bool VfsRemove(VfsId dir, VfsId node);
    sl::Opt<VfsId> VfsFindChild(VfsId dir, sl::StringSpan name);
    sl::Opt<NodeAttribs> VfsGetAttribs(VfsId node);
    bool VfsSetAttribs(VfsId node, const NodeAttribs& attribs, NodeAttribFlags selected);
    sl::Opt<DirEntries> VfsReadDir(VfsId node);
}
