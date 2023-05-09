#pragma once

#include <filesystem/Vfs.h>

namespace Npk::Filesystem
{
    void InitVfs();
    VfsDriver* RootFs();
    sl::Handle<Node> VfsLookup(sl::StringSpan path, const FsContext& context);
}
