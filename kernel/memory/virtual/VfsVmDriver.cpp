#include <memory/virtual/VfsVmDriver.h>
#include <boot/CommonInit.h>
#include <debug/Log.h>
#include <filesystem/Filesystem.h>
#include <filesystem/FileCache.h>
#include <Memory.h>

namespace Npk::Memory::Virtual
{
    struct VfsVmLink
    {
        size_t hatMode;
        bool readonly;
        sl::Handle<Filesystem::Node> vfsNode;
    };

    void VfsVmDriver::Init(uintptr_t enableFeatures)
    { 
        features.demandPage = enableFeatures & (uintptr_t)VfsFeature::Demand;

        Log("VmDriver init: vfs, demand=%s", LogLevel::Info, 
            features.demandPage ? "yes" : "no");
    }

    EventResult VfsVmDriver::HandleFault(VmDriverContext& context, uintptr_t where, VmFaultFlags flags)
    {
        ASSERT_UNREACHABLE()
    }

    QueryResult VfsVmDriver::Query(size_t length, VmFlags flags, uintptr_t attachArg)
    {
        ASSERT_UNREACHABLE()
        /* length is basically just the size of window rounded up to the cache size granularity
         * do a tentative check if file exists and is actually a file.
         */
    }

    bool VfsVmDriver::ModifyRange(VmDriverContext& context, sl::Opt<VmFlags> flags)
    {
        ASSERT_UNREACHABLE()
        /*
         * check file exists
         * attach this range to the file cache parts (in a list I guess) so we can traceback
         * this increments filecache ref count by 1?
         * do usual demand-page stuff (and issue io operations for file cache misses)
         */
    }

    AttachResult VfsVmDriver::Attach(VmDriverContext& context, const QueryResult& query, uintptr_t attachArg)
    {
        ASSERT_UNREACHABLE()
    }

    bool VfsVmDriver::Detach(VmDriverContext& context)
    {
        ASSERT_UNREACHABLE()
    }
}
