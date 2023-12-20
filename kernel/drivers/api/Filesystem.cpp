#include <drivers/api/Filesystem.h>
#include <filesystem/Filesystem.h>
#include <debug/Log.h>

extern "C"
{
    using namespace Npk;

    [[gnu::used]]
    npk_fs_id npk_fs_root()
    {
        ASSERT_UNREACHABLE();
    }

    [[gnu::used]]
    npk_fs_id npk_fs_lookup(npk_string path)
    {
        ASSERT_UNREACHABLE();
    }
}
