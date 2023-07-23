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

        Log("VmDriver init: vfs, demandCacheIn=%s", LogLevel::Info, 
            features.demandPage ? "yes" : "no");
    }

    EventResult VfsVmDriver::HandleFault(VmDriverContext& context, uintptr_t where, VmFaultFlags flags)
    {
        ASSERT_UNREACHABLE()
    }

    QueryResult VfsVmDriver::Query(size_t length, VmFlags flags, uintptr_t attachArg)
    {
        ASSERT_UNREACHABLE()
    }

    bool VfsVmDriver::ModifyRange(VmDriverContext& context, sl::Opt<VmFlags> flags)
    {
        ASSERT_UNREACHABLE()
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
