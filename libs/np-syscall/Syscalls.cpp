#include <SyscallEnums.h>
#include <Syscalls.h>

namespace np::Syscall
{
    bool SyscallLoopbackSuccess()
    {
        SyscallData data((uint64_t)SyscallId::LoopbackTest, 0, 0, 0, 0);
        DoSyscall(&data);

        return data.id == 0;
    }

    sl::Opt<PrimaryFramebufferData> GetPrimaryFramebuffer()
    {
        SyscallData data((uint64_t)SyscallId::GetPrimaryFramebuffer, 0, 0, 0, 0);
        DoSyscall(&data);

        if (data.id != 0)
            return {};

        PrimaryFramebufferData fbData;
        fbData.baseAddress = data.arg0;
        fbData.width = data.arg1 & 0xFFFF'FFFF;
        fbData.height = data.arg1 >> 32;
        fbData.bpp = data.arg2 & 0xFFFF'FFFF;
        fbData.stride = data.arg2 >> 32;
        fbData.format.raw = data.arg3;
        
        return fbData;
    }
}
