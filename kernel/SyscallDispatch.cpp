#include <SyscallDispatch.h>
#include <Syscalls.h>
#include <SyscallEnums.h>
#include <devices/DeviceManager.h>
#include <devices/interfaces/GenericFramebuffer.h>

namespace Kernel
{
    StoredRegisters* DispatchSyscall(StoredRegisters* regs)
    {
        //TODO: enable interrupts, send EOI and mark this iret frame as having been handled (but not returned from). Lets us being pre-empted within syscalls.
        //or
        //we could modify iret frame to return to kernel ss:rsp and cs:rip of interrupt handling code. This would 'just work' with existing scheduling stuff.
        
        using np::Syscall::SyscallId;
        SyscallId attemptedId = static_cast<SyscallId>(regs->rax);
        switch (attemptedId)
        {
        case SyscallId::LoopbackTest:
            regs->rax = 0;
            break;
        case SyscallId::GetPrimaryFramebuffer:
        {
            regs->rax = 0; //TODO: lots of magic numbers here, would be good to move them to constants

            using namespace Devices;
            sl::Opt<GenericDevice*> maybePrimaryFramebuffer = DeviceManager::Global()->GetPrimaryDevice(DeviceType::GraphicsFramebuffer);
            if (!maybePrimaryFramebuffer)
            {
                regs->rax = 1;
                break;
            }

            Interfaces::GenericFramebuffer* fbDesc = static_cast<Interfaces::GenericFramebuffer*>(*maybePrimaryFramebuffer);
            regs->rdi = fbDesc->GetAddress().Value().raw;
            Interfaces::FramebufferModeset modeset = fbDesc->GetCurrentMode();
            regs->rsi = modeset.width | (modeset.height << 32);
            regs->rdx = modeset.bitsPerPixel | ((modeset.width * modeset.bitsPerPixel / 8) << 32);

#define SQUISH_INTO_REG(field, shift) ((uint64_t)modeset.pixelFormat.field << shift)
            regs->rcx = SQUISH_INTO_REG(redOffset, 0) | SQUISH_INTO_REG(greenOffset, 8) | SQUISH_INTO_REG(blueOffset, 16) | SQUISH_INTO_REG(alphaOffset, 24)
                | SQUISH_INTO_REG(redMask, 32) | SQUISH_INTO_REG(greenMask, 40) | SQUISH_INTO_REG(blueMask, 48) | SQUISH_INTO_REG(alphaMask, 56);
#undef SQUISH_INTO_REG
            break;
        }

        default:
            regs->rax = 0x404; //couldnt find matching system call
            break;
        }

        return regs;
    }
}
