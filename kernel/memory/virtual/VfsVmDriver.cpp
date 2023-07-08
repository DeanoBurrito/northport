#include <memory/virtual/VfsVmDriver.h>
#include <debug/Log.h>
#include <filesystem/Vfs.h>

namespace Npk::Memory::Virtual
{
    void VfsVmDriver::Init(uintptr_t enableFeatures)
    { 
        (void)enableFeatures;
        Log("VmDriver init: vfs", LogLevel::Info);
    }

    EventResult VfsVmDriver::HandleFault(VmDriverContext& context, uintptr_t where, VmFaultFlags flags)
    {
        ASSERT_UNREACHABLE()
    }

    AttachResult VfsVmDriver::Attach(VmDriverContext& context, uintptr_t attachArg)
    {
        ASSERT_UNREACHABLE();
    }

    bool VfsVmDriver::Detach(VmDriverContext& context)
    {
        ASSERT_UNREACHABLE()
    }
}
